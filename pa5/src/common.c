#include <stdio.h>
#include <stdlib.h>

#include "common.h"

const char* invalid_format = "Error: invalid input\n";

/**
 * Struct to store minimially-processed input
 * for box data
 */
#define NUM_DIR 4
typedef enum { TOP=0, BOTTOM, LEFT, RIGHT } DIRECTION;
typedef struct BoxInput {
    Coord x_min, x_max, y_min, y_max;
    Coord perimeter;

    /**
     * [0] = [TOP] corresponds to top neighbors
     * [1] = [BOTTOM] corresponds to bottom neighbors
     * [2] = [LEFT] corresponds to left neighbors
     * [3] = [RIGHT] corresponds to right neighbors
     */
    Count num_nhbrs[NUM_DIR];
    Count* nhbr_ids[NUM_DIR];
} BoxInput;

/**
 * {@inheritDoc}
 */
AMRInput* parseInput(char* file_name) {
    FILE* file = fopen(file_name, "r");

    /**
     * Allocate struct
     */
    AMRInput* input = malloc(sizeof(*input));

    /**
     * Read in general parameters
     */
    if (fscanf(file, COUNT_SPEC, &input->N) != 1) {
        fprintf(stderr, "%s", invalid_format);
        exit(1);
    }
    if (fscanf(file, COORD_SPEC, &input->rows) != 1) {
        fprintf(stderr, "%s", invalid_format);
        exit(1);
    }
    if (fscanf(file, COORD_SPEC, &input->cols) != 1) {
        fprintf(stderr, "%s", invalid_format);
        exit(1);
    }

    /**
     * Allocate arrays for per-box values
     */
    BoxInput* bs = malloc(input->N * sizeof(*bs));
    input->vals  = malloc(input->N * sizeof(*input->vals));

    /**
     * Reading in per-box information
     */
    input->total_nhbrs = 0;
    for (int i = 0; i < input->N; ++i) {
        BoxInput* box_input = &bs[i];

        /**
         * Verify that ids are sequential
         */
        Count id;
        #ifdef DEBUG
        if (fscanf(file, COUNT_SPEC, &id) != 1) {
            fprintf(stderr, "%s", invalid_format);
            exit(1);
        }
        if (id != i) {
            fprintf(stderr, "%s", invalid_format);
            exit(1);
        }
        #else
        int rv = fscanf(file, COUNT_SPEC, &id);
        #endif

        /**
         * Size and position information
         */
        Coord y, x, height, width;
        if (fscanf(file, COORD_SPEC COORD_SPEC COORD_SPEC COORD_SPEC,
                  &y,        &x,        &height,   &width)
            != 4) {
            fprintf(stderr, "%s", invalid_format);
            exit(1);
        }
        box_input->x_min     = x;
        box_input->x_max     = x + width;
        box_input->y_min     = y;
        box_input->y_max     = y + height;
        box_input->perimeter = 2 * (height + width);

        /**
         * Topology information
         */
        for (DIRECTION dir = TOP; dir <= RIGHT; ++dir) {
            Count num_nhbrs;
            if (fscanf(file, COUNT_SPEC, &num_nhbrs) != 1) {
                fprintf(stderr, "%s", invalid_format);
                exit(1);
            }
            box_input->num_nhbrs[dir] = num_nhbrs;
            box_input->nhbr_ids[dir]  = malloc(
                num_nhbrs * sizeof(*box_input->nhbr_ids[dir])
            );
            for (int nhbr = 0; nhbr < num_nhbrs; ++nhbr) {
                if (fscanf(file, COUNT_SPEC, &box_input->nhbr_ids[dir][nhbr]) != 1) {
                    fprintf(stderr, "%s", invalid_format);
                    exit(1);
                }
            }
            input->total_nhbrs += num_nhbrs;
        }

        /**
         * DSV information
         */
        if (fscanf(file, DSV_SPEC, &input->vals[i]) != 1) {
            fprintf(stderr, "%s", invalid_format);
            exit(1);
        }
    }
    fclose(file);

    /**
     * Processes box data (currently stored in {@code bs})
     * to a more convenient format (to be stored in {@code boxes}).
     */
    input->nhbr_ids = malloc(input->total_nhbrs * sizeof(*input->nhbr_ids));
    input->overlaps = malloc(input->total_nhbrs * sizeof(*input->overlaps));

    input->perimeters    = malloc(input->N * sizeof(*input->perimeters));
    input->num_nhbrs     = malloc(input->N * sizeof(*input->num_nhbrs));
    input->offsets       = malloc(input->N * sizeof(*input->offsets));
    input->self_overlaps = malloc(input->N * sizeof(*input->self_overlaps));
    Count offset = 0;
    for (int i = 0; i < input->N; ++i) {
        BoxInput* box_input = &bs[i];

        input->perimeters[i] = box_input->perimeter;

        input->num_nhbrs[i] = 0;
        for (DIRECTION dir = TOP; dir <= RIGHT; ++dir) {
            input->num_nhbrs[i] += box_input->num_nhbrs[dir];
        }

        input->offsets[i] = offset;
        offset += input->num_nhbrs[i];
        
        input->self_overlaps[i] = input->perimeters[i];
        Count total_nhbr = 0;
        for (int nhbr = 0;
             nhbr < box_input->num_nhbrs[TOP];
             ++nhbr, ++total_nhbr
        ) {
            Count nhbr_id = box_input->nhbr_ids[TOP][nhbr];
            input->nhbr_ids[input->offsets[i] + total_nhbr] = nhbr_id;
            Coord x_max = min(box_input->x_max, bs[nhbr_id].x_max);
            Coord x_min = max(box_input->x_min, bs[nhbr_id].x_min);
            input->overlaps[input->offsets[i] + total_nhbr] = x_max - x_min;
            input->self_overlaps[i] -= x_max - x_min;
        }
        for (int nhbr = 0;
             nhbr < box_input->num_nhbrs[BOTTOM];
             ++nhbr, ++total_nhbr
        ) {
            Count nhbr_id = box_input->nhbr_ids[BOTTOM][nhbr];
            input->nhbr_ids[input->offsets[i] + total_nhbr] = nhbr_id;
            Coord x_max = min(box_input->x_max, bs[nhbr_id].x_max);
            Coord x_min = max(box_input->x_min, bs[nhbr_id].x_min);
            input->overlaps[input->offsets[i] + total_nhbr] = x_max - x_min;
            input->self_overlaps[i] -= x_max - x_min;
        }
        for (int nhbr = 0;
             nhbr < box_input->num_nhbrs[LEFT];
             ++nhbr, ++total_nhbr
        ) {
            Count nhbr_id = box_input->nhbr_ids[LEFT][nhbr];
            input->nhbr_ids[input->offsets[i] + total_nhbr] = nhbr_id;
            Coord y_max = min(box_input->y_max, bs[nhbr_id].y_max);
            Coord y_min = max(box_input->y_min, bs[nhbr_id].y_min);
            input->overlaps[input->offsets[i] + total_nhbr] = y_max - y_min;
            input->self_overlaps[i] -= y_max - y_min;
        }
        for (int nhbr = 0;
             nhbr < box_input->num_nhbrs[RIGHT];
             ++nhbr, ++total_nhbr
        ) {
            Count nhbr_id = box_input->nhbr_ids[RIGHT][nhbr];
            input->nhbr_ids[input->offsets[i] + total_nhbr] = nhbr_id;
            Coord y_max = min(box_input->y_max, bs[nhbr_id].y_max);
            Coord y_min = max(box_input->y_min, bs[nhbr_id].y_min);
            input->overlaps[input->offsets[i] + total_nhbr] = y_max - y_min;
            input->self_overlaps[i] -= y_max - y_min;
        }
    }

    /**
     * Clean up
     */
    for (int i = 0; i < input->N; ++i) {
        BoxInput* box_input = &bs[i];
        for (DIRECTION dir = TOP; dir <= RIGHT; ++dir) {
            free(box_input->nhbr_ids[dir]);
        }
    }
    free(bs);

    return input;
}

/**
 * {@inheritDoc}
 */
void destroyInput(AMRInput* input) {
    free(input->perimeters);
    free(input->num_nhbrs);
    free(input->offsets);
    free(input->self_overlaps);
    free(input->nhbr_ids);
    free(input->overlaps);
    free(input->vals);
    free(input);
}

/**
 * {@inheritDoc}
 */
void displayOutput(AMROutput output) {
    printf("========================================\n");
    printf("params:\n");
    printf("=> affect_rate %f\n", output.affect_rate);
    printf("=> epsilon     %f\n", output.epsilon);
    printf("\nresults:\n");
    printf("=> iterations %lu\n", output.iterations);
    printf("=> max-DSV    "DSV_SPEC"\n", output.max);
    printf("=> min-DSV    "DSV_SPEC"\n", output.min);
    printf("\ntiming:\n");
    printf("=> time-seconds    %lf\n", output.time_seconds);
    printf("=> clock-seconds   %lf\n", output.clock_seconds);
    printf("=> gettime-seconds %lf\n", output.gettime_seconds);
    printf("========================================\n\n");
}
