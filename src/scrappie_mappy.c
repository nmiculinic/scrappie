#include <math.h>
#include <stdio.h>
//#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include "decode.h"
#include "fast5_interface.h"
#include "kseq.h"
#include "networks.h"
#include "scrappie_common.h"
#include "scrappie_licence.h"
#include "scrappie_seq_helpers.h"
#include "scrappie_stdlib.h"
#include "util.h"

KSEQ_INIT(int, read)

// Doesn't play nice with other headers, include last
#include <argp.h>


extern const char *argp_program_version;
extern const char *argp_program_bug_address;
static char doc[] = "Scrappie squiggler";
static char args_doc[] = "fasta fast5";
static struct argp_option options[] = {
    {"backprob", 'b', "probability", 0, "Probability of backwards movement"},
    {"localpen", 'l', "float", 0, "Penalty for local matching"},
    {"minscore", 'm', "float", 0, "Minimum possible score for matching emission"},
    {"output", 'o', "filename", 0, "Write to file rather than stdout"},
    {"prefix", 'p', "string", 0, "Prefix to append to name of read"},
    {"segmentation", 's', "chunk:percentile", 0, "Chunk size and percentile for variance based segmentation"},
    {"trim", 't', "start:end", 0, "Number of samples to trim, as start:end"},
    {"licence", 10, 0, 0, "Print licensing information"},
    {"license", 11, 0, OPTION_ALIAS, "Print licensing information"},
    {0}
};

typedef struct {
    size_t n;
    char * seq;
    char * name;
} scrappie_seq_t;

struct arguments {
    float backprob;
    float localpen;
    float minscore;
    FILE * output;
    char * prefix;
    int trim_start;
    int trim_end;
    int varseg_chunk;
    float varseg_thresh;

    char * fasta_file;
    char * fast5_file;
};

static struct arguments args = {
    .backprob = 0.0f,
    .localpen = 2.0f,
    .minscore = 5.0f,
    .output = NULL,
    .prefix = "",
    .trim_start = 200,
    .trim_end = 10,
    .varseg_chunk = 100,
    .varseg_thresh = 0.0f,

    .fasta_file = NULL,
    .fast5_file = NULL
};

static error_t parse_arg(int key, char *arg, struct argp_state *state) {
    switch (key) {
        int ret = 0;
        char * next_tok = NULL;
    case 'b':
        args.backprob = atof(arg);
        if(args.backprob < 0.0 && args.backprob >= 1.0){
            errx(EXIT_FAILURE, "Backwards probability must be in [0, 1). Got %f", args.backprob);
        }
        break;
    case 'l':
        args.localpen = atof(arg);
        break;
    case 'm':
        args.minscore = atof(arg);
        break;
    case 'o':
        args.output = fopen(arg, "w");
        if(NULL == args.output){
            errx(EXIT_FAILURE, "Failed to open \"%s\" for output.", arg);
        }
        break;
    case 'p':
        args.prefix = arg;
        break;
    case 's':
        args.varseg_chunk = atoi(strtok(arg, ":"));
        next_tok = strtok(NULL, ":");
        if(NULL == next_tok){
            errx(EXIT_FAILURE, "--segmentation should be of form chunk:percentile");
        }
        args.varseg_thresh = atof(next_tok) / 100.0;
        assert(args.varseg_chunk >= 0);
        assert(args.varseg_thresh > 0.0 && args.varseg_thresh < 1.0);
        break;
    case 't':
        args.trim_start = atoi(strtok(arg, ":"));
        next_tok = strtok(NULL, ":");
        if(NULL != next_tok){
            args.trim_end = atoi(next_tok);
        } else {
            args.trim_end = args.trim_start;
        }
        assert(args.trim_start >= 0);
        assert(args.trim_end >= 0);
        break;
    case 10:
    case 11:
        ret = fputs(scrappie_licence_text, stdout);
        exit((EOF != ret) ? EXIT_SUCCESS : EXIT_FAILURE);
        break;

    case ARGP_KEY_NO_ARGS:
        argp_usage(state);
        break;

    case ARGP_KEY_ARG:
        args.fasta_file = state->argv[state->next - 1];
        if(NULL == state->argv[state->next]){
            errx(EXIT_FAILURE, "fast5 file is a required argument");
        }
        args.fast5_file = state->argv[state->next];
        state->next = state->argc;
        break;

    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_arg, args_doc, doc };


static scrappie_matrix sequence_to_squiggle(char const * base_seq, size_t n, bool rescale){
    RETURN_NULL_IF(NULL == base_seq, NULL);

    int * sequence = encode_bases_to_integers(base_seq, n);
    RETURN_NULL_IF(NULL == sequence, NULL);

    scrappie_matrix squiggle = dna_squiggle(sequence, n, rescale);
    free(sequence);

    return squiggle;
}

/**  Map signal to a predicted squiggle
 *
 *
 *   @returns array
 **/
static int * map_signal_to_squiggle(const raw_table signal, const_scrappie_matrix squiggle,
                                    float backprob, float localpen, float minscore){
    RETURN_NULL_IF(NULL == signal.raw, NULL);
    RETURN_NULL_IF(NULL == squiggle, NULL);

    int32_t * path = calloc(signal.n, sizeof(int32_t));
    RETURN_NULL_IF(NULL == path, NULL);

    (void)squiggle_match_viterbi(signal, squiggle, backprob, localpen, minscore, path);

    return path;
}


static scrappie_seq_t read_sequence_from_fasta(char const * filename){
    scrappie_seq_t seq = {0, NULL, NULL};

    FILE * fh = fopen(filename, "r");
    if(NULL == fh){
        return seq;
    }

    kseq_t * kseqer = kseq_init(fileno(fh));
    if(kseq_read(kseqer) >= 0){
        char * name = calloc(kseqer->name.l + 1, sizeof(char));
        char * base_seq = calloc(kseqer->seq.l, sizeof(char));
        if(NULL == base_seq || NULL == name){
            free(base_seq);
            free(name);
        } else {
            seq.seq = strncpy(base_seq, kseqer->seq.s, kseqer->seq.l);
            seq.name = strncpy(name, kseqer->name.s, kseqer->name.l);
            seq.n = kseqer->seq.l;
        }
    }

    kseq_destroy(kseqer);
    fclose(fh);

    return seq;
}

int main_mappy(int argc, char *argv[]) {
    argp_parse(&argp, argc, argv, 0, 0, NULL);
    if(NULL == args.output){
        args.output = stdout;
    }


    //  Open sequence file
    scrappie_seq_t seq = read_sequence_from_fasta(args.fasta_file);
    if(NULL == seq.seq){
        warnx("Failed to open \"%s\" for input.\n", args.fasta_file);
        return EXIT_FAILURE;
    }

    //  Read raw signal and normalise
    raw_table rt = read_raw(args.fast5_file, true);
    rt = trim_and_segment_raw(rt, args.trim_start, args.trim_end, args.varseg_chunk, args.varseg_thresh);
    if(NULL == rt.raw){
        warnx("Failed to open \"%s\" for input and trim signal.\n", args.fasta_file);
        return EXIT_FAILURE;
    }
    medmad_normalise_array(rt.raw + rt.start, rt.end - rt.start);



    scrappie_matrix squiggle = sequence_to_squiggle(seq.seq, seq.n, false);
	if(NULL != squiggle){
        int * path = map_signal_to_squiggle(rt, squiggle, args.backprob, args.localpen, args.minscore);
        if(NULL != path){
            fprintf(args.output, "# %s to %s\n", args.fast5_file, args.fasta_file);
            fprintf(args.output, "sample\tpos\tbase\tcurrent\tsd\tdwell\n");
            for(size_t i=0 ; i < rt.n ; i++){
                const int32_t pos = path[i];
                if(pos >= 0){
                    const size_t offset = pos * squiggle->stride;
                    fprintf(args.output, "%zu\t%d\t%c\t%3.6f\t%3.6f\t%3.6f\n", i, pos, seq.seq[pos],
                            squiggle->data.f[offset + 0],
                            expf(squiggle->data.f[offset + 1]),
                            expf(-squiggle->data.f[offset + 2]));
                } else {
                    fprintf(args.output, "%zu\t%d\tN\tNaN\tNaN\tNaN\n", i, pos);
                }
            }
            free(path);
        }
		squiggle = free_scrappie_matrix(squiggle);
	}


    free(seq.seq);
    free(seq.name);

    return EXIT_SUCCESS;
}
