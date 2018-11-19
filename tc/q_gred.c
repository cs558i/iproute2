/*
 * q_gred.c		GRED.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:    J Hadi Salim(hadi@nortelnetworks.com)
 *             code ruthlessly ripped from
 *	       Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <math.h>

#include "utils.h"
#include "tc_util.h"

#include "tc_red.h"


#if 0
#define DPRINTF(format, args...) fprintf(stderr, format, ##args)
#else
#define DPRINTF(format, args...)
#endif

static void explain(void)
{
	fprintf(stderr, "Usage: tc qdisc { add | replace | change } ... gred setup vqs NUMBER\n");
	fprintf(stderr, "           default DEFAULT_VQ [ grio ] [ limit BYTES ]\n");
	fprintf(stderr, "       tc qdisc change ... gred vq VQ [ prio VALUE ] limit BYTES\n");
	fprintf(stderr, "           min BYTES max BYTES avpkt BYTES [ burst PACKETS ]\n");
	fprintf(stderr, "           [ probability PROBABILITY ] [ bandwidth KBPS ]\n");
}

static int init_gred(struct qdisc_util *qu, int argc, char **argv,
		     struct nlmsghdr *n)
{

	struct rtattr *tail;
	struct tc_gred_sopt opt = { 0 };
	__u32 limit = 0;

	opt.def_DP = MAX_DPs;

	while (argc > 0) {
		DPRINTF(stderr, "init_gred: invoked with %s\n", *argv);
		if (strcmp(*argv, "vqs") == 0 ||
		    strcmp(*argv, "DPs") == 0) {
			NEXT_ARG();
			if (get_unsigned(&opt.DPs, *argv, 10)) {
				fprintf(stderr, "Illegal \"vqs\"\n");
				return -1;
			} else if (opt.DPs > MAX_DPs) {
				fprintf(stderr, "GRED: only %u VQs are currently supported\n",
					MAX_DPs);
				return -1;
			}
		} else if (strcmp(*argv, "default") == 0) {
			if (opt.DPs == 0) {
				fprintf(stderr, "\"default\" must be defined after \"vqs\"\n");
				return -1;
			}
			NEXT_ARG();
			if (get_unsigned(&opt.def_DP, *argv, 10)) {
				fprintf(stderr, "Illegal \"default\"\n");
				return -1;
			} else if (opt.def_DP >= opt.DPs) {
				fprintf(stderr, "\"default\" must be less than \"vqs\"\n");
				return -1;
			}
		} else if (strcmp(*argv, "grio") == 0) {
			opt.grio = 1;
		} else if (strcmp(*argv, "limit") == 0) {
			NEXT_ARG();
			if (get_size(&limit, *argv)) {
				fprintf(stderr, "Illegal \"limit\"\n");
				return -1;
			}
		} else if (strcmp(*argv, "help") == 0) {
			explain();
			return -1;
		} else {
			fprintf(stderr, "What is \"%s\"?\n", *argv);
			explain();
			return -1;
		}
		argc--; argv++;
	}

	if (!opt.DPs || opt.def_DP == MAX_DPs) {
		fprintf(stderr, "Illegal gred setup parameters\n");
		return -1;
	}

	DPRINTF("TC_GRED: sending DPs=%u def_DP=%u\n", opt.DPs, opt.def_DP);
	n->nlmsg_flags |= NLM_F_CREATE;
	tail = addattr_nest(n, 1024, TCA_OPTIONS);
	addattr_l(n, 1024, TCA_GRED_DPS, &opt, sizeof(struct tc_gred_sopt));
	if (limit)
		addattr32(n, 1024, TCA_GRED_LIMIT, limit);
	addattr_nest_end(n, tail);
	return 0;
}
/*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
*/
static int gred_parse_opt(struct qdisc_util *qu, int argc, char **argv, struct nlmsghdr *n, const char *dev)
{
	int ok = 0;
	struct tc_gred_qopt opt = { 0 };
	unsigned int burst = 0;
	unsigned int avpkt = 0;
	double probability = 0.02;
	unsigned int rate = 0;
	int parm;
	__u8 sbuf[256];
	struct rtattr *tail;
	__u32 max_P;

	opt.DP = MAX_DPs;

	while (argc > 0) {
		if (strcmp(*argv, "limit") == 0) {
			NEXT_ARG();
			if (get_size(&opt.limit, *argv)) {
				fprintf(stderr, "Illegal \"limit\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "setup") == 0) {
			if (ok) {
				fprintf(stderr, "Illegal \"setup\"\n");
				return -1;
			}
			return init_gred(qu, argc-1, argv+1, n);
		} else if (strcmp(*argv, "min") == 0) {
			NEXT_ARG();
			if (get_size(&opt.qth_min, *argv)) {
				fprintf(stderr, "Illegal \"min\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "max") == 0) {
			NEXT_ARG();
			if (get_size(&opt.qth_max, *argv)) {
				fprintf(stderr, "Illegal \"max\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "vq") == 0 ||
			   strcmp(*argv, "DP") == 0) {
			NEXT_ARG();
			if (get_unsigned(&opt.DP, *argv, 10)) {
				fprintf(stderr, "Illegal \"vq\"\n");
				return -1;
			} else if (opt.DP >= MAX_DPs) {
				fprintf(stderr, "GRED: only %u VQs are currently supported\n",
					MAX_DPs);
				return -1;
			} /* need a better error check */
			ok++;
		} else if (strcmp(*argv, "burst") == 0) {
			NEXT_ARG();
			if (get_unsigned(&burst, *argv, 0)) {
				fprintf(stderr, "Illegal \"burst\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "avpkt") == 0) {
			NEXT_ARG();
			if (get_size(&avpkt, *argv)) {
				fprintf(stderr, "Illegal \"avpkt\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "probability") == 0) {
			NEXT_ARG();
			if (sscanf(*argv, "%lg", &probability) != 1) {
				fprintf(stderr, "Illegal \"probability\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "prio") == 0) {
			NEXT_ARG();
			opt.prio = strtol(*argv, (char **)NULL, 10);
			/* some error check here */
			ok++;
		} else if (strcmp(*argv, "bandwidth") == 0) {
			NEXT_ARG();
			if (strchr(*argv, '%')) {
				if (get_percent_rate(&rate, *argv, dev)) {
					fprintf(stderr, "Illegal \"bandwidth\"\n");
					return -1;
				}
			} else if (get_rate(&rate, *argv)) {
				fprintf(stderr, "Illegal \"bandwidth\"\n");
				return -1;
			}
			ok++;
		} else if (strcmp(*argv, "help") == 0) {
			explain();
			return -1;
		} else {
			fprintf(stderr, "What is \"%s\"?\n", *argv);
			explain();
			return -1;
		}
		argc--; argv++;
	}

	if (!ok) {
		explain();
		return -1;
	}
	if (opt.DP == MAX_DPs || !opt.limit || !opt.qth_min || !opt.qth_max ||
	    !avpkt) {
		fprintf(stderr, "Required parameter (vq, limit, min, max, avpkt) is missing\n");
		return -1;
	}
	if (!burst) {
		burst = (2 * opt.qth_min + opt.qth_max) / (3 * avpkt);
		fprintf(stderr, "GRED: set burst to %u\n", burst);
	}
	if (!rate) {
		get_rate(&rate, "10Mbit");
		fprintf(stderr, "GRED: set bandwidth to 10Mbit\n");
	}
	if ((parm = tc_red_eval_ewma(opt.qth_min, burst, avpkt)) < 0) {
		fprintf(stderr, "GRED: failed to calculate EWMA constant.\n");
		return -1;
	}
	if (parm >= 10)
		fprintf(stderr, "GRED: WARNING. Burst %u seems to be too large.\n",
		    burst);
	opt.Wlog = parm;
	if ((parm = tc_red_eval_P(opt.qth_min, opt.qth_max, probability)) < 0) {
		fprintf(stderr, "GRED: failed to calculate probability.\n");
		return -1;
	}
	opt.Plog = parm;
	if ((parm = tc_red_eval_idle_damping(opt.Wlog, avpkt, rate, sbuf)) < 0)
	    {
		fprintf(stderr, "GRED: failed to calculate idle damping table.\n");
		return -1;
	}
	opt.Scell_log = parm;

	tail = addattr_nest(n, 1024, TCA_OPTIONS);
	addattr_l(n, 1024, TCA_GRED_PARMS, &opt, sizeof(opt));
	addattr_l(n, 1024, TCA_GRED_STAB, sbuf, 256);
	max_P = probability * pow(2, 32);
	addattr32(n, 1024, TCA_GRED_MAX_P, max_P);
	addattr_nest_end(n, tail);
	return 0;
}

static void gred_print_stats(struct tc_gred_qopt *qopt)
{
	SPRINT_BUF(b1);

	if (!is_json_context())
		printf("\n  Queue size: ");

	print_uint(PRINT_JSON, "qave", NULL, qopt->qave);
	print_string(PRINT_FP, NULL, "average %s ",
		     sprint_size(qopt->qave, b1));

	print_uint(PRINT_JSON, "backlog", NULL, qopt->backlog);
	print_string(PRINT_FP, NULL, "current %s ",
		     sprint_size(qopt->backlog, b1));

	if (!is_json_context())
		printf("\n  Dropped packets: ");

	print_uint(PRINT_ANY, "forced_drop", "forced %u ", qopt->forced);
	print_uint(PRINT_ANY, "prob_drop", "early %u ", qopt->early);
	print_uint(PRINT_ANY, "pdrop", "pdrop %u ", qopt->pdrop);
	print_uint(PRINT_ANY, "other", "other %u ", qopt->other);

	if (!is_json_context())
		printf("\n  Total packets: ");

	print_uint(PRINT_ANY, "packets", "%u ", qopt->packets);

	print_uint(PRINT_JSON, "bytes", NULL, qopt->bytesin);
	print_string(PRINT_FP, NULL, "(%s) ", sprint_size(qopt->bytesin, b1));
}

static int gred_print_opt(struct qdisc_util *qu, FILE *f, struct rtattr *opt)
{
	struct rtattr *tb[TCA_GRED_MAX + 1];
	struct tc_gred_sopt *sopt;
	struct tc_gred_qopt *qopt;
	__u32 *max_p = NULL;
	__u32 *limit = NULL;
	unsigned int i;

	SPRINT_BUF(b1);

	if (opt == NULL)
		return 0;

	parse_rtattr_nested(tb, TCA_GRED_MAX, opt);

	if (tb[TCA_GRED_PARMS] == NULL)
		return -1;

	if (tb[TCA_GRED_MAX_P] &&
	    RTA_PAYLOAD(tb[TCA_GRED_MAX_P]) >= sizeof(__u32) * MAX_DPs)
		max_p = RTA_DATA(tb[TCA_GRED_MAX_P]);

	if (tb[TCA_GRED_LIMIT] &&
	    RTA_PAYLOAD(tb[TCA_GRED_LIMIT]) == sizeof(__u32))
		limit = RTA_DATA(tb[TCA_GRED_LIMIT]);

	sopt = RTA_DATA(tb[TCA_GRED_DPS]);
	qopt = RTA_DATA(tb[TCA_GRED_PARMS]);
	if (RTA_PAYLOAD(tb[TCA_GRED_DPS]) < sizeof(*sopt) ||
	    RTA_PAYLOAD(tb[TCA_GRED_PARMS]) < sizeof(*qopt)*MAX_DPs) {
		fprintf(f, "\n GRED received message smaller than expected\n");
		return -1;
	}

	print_uint(PRINT_ANY, "dp_cnt", "vqs %u ", sopt->DPs);
	print_uint(PRINT_ANY, "dp_default", "default %u ", sopt->def_DP);

	if (sopt->grio)
		print_bool(PRINT_ANY, "grio", "grio ", true);
	else
		print_bool(PRINT_ANY, "grio", NULL, false);

	if (limit) {
		print_uint(PRINT_JSON, "limit", NULL, *limit);
		print_string(PRINT_FP, NULL, "limit %s ",
			     sprint_size(*limit, b1));
	}

	open_json_array(PRINT_JSON, "vqs");
	for (i = 0; i < MAX_DPs; i++, qopt++) {
		if (qopt->DP >= MAX_DPs)
			continue;

		open_json_object(NULL);

		print_uint(PRINT_ANY, "vq", "\n vq %u ", qopt->DP);
		print_hhu(PRINT_ANY, "prio", "prio %hhu ", qopt->prio);

		print_uint(PRINT_JSON, "limit", NULL, qopt->limit);
		print_string(PRINT_FP, NULL, "limit %s ",
			     sprint_size(qopt->limit, b1));

		print_uint(PRINT_JSON, "min", NULL, qopt->qth_min);
		print_string(PRINT_FP, NULL, "min %s ",
			     sprint_size(qopt->qth_min, b1));

		print_uint(PRINT_JSON, "max", NULL, qopt->qth_max);
		print_string(PRINT_FP, NULL, "max %s ",
			     sprint_size(qopt->qth_max, b1));

		if (show_details) {
			print_uint(PRINT_ANY, "ewma", "ewma %u ", qopt->Wlog);
			if (max_p)
				print_float(PRINT_ANY, "probability",
					    "probability %lg ",
					    max_p[i] / pow(2, 32));
			else
				print_uint(PRINT_ANY, "Plog", "Plog %u ",
					   qopt->Plog);
			print_uint(PRINT_ANY, "Scell_log", "Scell_log %u ",
				   qopt->Scell_log);
		}
		if (show_stats)
			gred_print_stats(qopt);
		close_json_object();
	}
	close_json_array(PRINT_JSON, "vqs");
	return 0;
}

struct qdisc_util gred_qdisc_util = {
	.id		= "gred",
	.parse_qopt	= gred_parse_opt,
	.print_qopt	= gred_print_opt,
};
