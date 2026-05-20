#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"

#define MAX_REFS 64

struct HeapObject {
    char ID;
    int address;
    char ref_names[MAX_REFS];
    int num_ref_names;
    struct HeapObject* references[MAX_REFS];
    int num_references;

    int ref_count;
    bool marked;
    // for mark & compact
    int new_address;
};

