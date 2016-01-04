/*
 * Copyright (c) 2015 Mikhail Baranov
 * Copyright (c) 2015 Victor Gaydov
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_core/log.h"
#include "roc_datagram/address_to_str.h"
#include "roc_datagram/datagram_queue.h"
#include "roc_audio/sample_buffer_queue.h"
#include "roc_pipeline/server.h"
#include "roc_rtp/parser.h"
#include "roc_sndio/writer.h"
#include "roc_netio/transceiver.h"
#include "roc_netio/inet_address.h"

#include "roc_recv/cmdline.h"

using namespace roc;

int main(int argc, char** argv) {
    gengetopt_args_info args;

    const int code = cmdline_parser(argc, argv, &args);
    if (code != 0) {
        return code;
    }

    if (args.inputs_num != 1) {
        fprintf(stderr, "%s\n", gengetopt_args_info_usage);
        return 1;
    }

    core::set_log_level(LogLevel(LOG_ERROR + args.verbose_given));

    datagram::Address addr;
    if (!netio::parse_address(args.inputs[0], addr)) {
        roc_log(LOG_ERROR, "can't parse address: %s", args.inputs[0]);
        return 1;
    }

    pipeline::ServerConfig config;
    if (args.fec_arg == fec_arg_ldpc) {
        config.options |= pipeline::EnableLDPC;
    }
    if (args.resampling_arg == resampling_arg_yes) {
        config.options |= pipeline::EnableResampling;
    }
    if (args.timing_arg == timing_arg_yes) {
        config.options |= pipeline::EnableTiming;
    }
    if (args.oneshot_flag) {
        config.options |= pipeline::EnableOneshot;
    }
    if (args.beep_flag) {
        config.options |= pipeline::EnableBeep;
    }
    if (args.latency_given) {
        if (args.latency_arg < 0) {
            roc_log(LOG_ERROR, "invalid `--latency %d': should be >= 0",
                    args.latency_arg);
            return 1;
        }
        config.latency = (packet::timestamp_t)args.latency_arg;
    }

    datagram::DatagramQueue dgm_queue;
    audio::SampleBufferQueue sample_queue;
    rtp::Parser rtp_parser;

    netio::Transceiver trx;
    if (!trx.add_udp_receiver(addr, dgm_queue)) {
        roc_log(LOG_ERROR, "can't register udp receiver: %s",
                datagram::address_to_str(addr).c_str());
        return 1;
    }

    pipeline::Server server(dgm_queue, sample_queue, config);
    server.add_port(addr, rtp_parser);

    sndio::Writer writer(sample_queue);
    if (!writer.open(args.output_arg, args.type_arg)) {
        roc_log(LOG_ERROR, "can't open output file/device: %s %s", args.output_arg,
                args.type_arg);
        return 1;
    }

    trx.start();

    writer.start();

    server.start();
    server.join();

    writer.join();

    trx.stop();
    trx.join();

    return 0;
}
