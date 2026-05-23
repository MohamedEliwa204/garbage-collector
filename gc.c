#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "cJSON.h"

#define MAX_REFS 64
#define MAX_NAME_LEN 32
#define MAX_LINE_LENGTH 8192
typedef struct HeapObject
{
    char ID[MAX_NAME_LEN];
    int address;
    char ref_names[MAX_REFS][MAX_NAME_LEN];
    int num_ref_names;
    struct HeapObject *references[MAX_REFS];
    int num_references;

    int ref_count;
    bool marked;
    // for mark & compact
    int new_address;
} HeapObject;

typedef struct
{
    char root_names[MAX_REFS][MAX_NAME_LEN];
    HeapObject *roots[MAX_REFS];
    int num_roots;
    HeapObject objects[MAX_REFS];
    int object_count;
} GCEnvironment;

void reset_metadata(GCEnvironment *env);
HeapObject *find_object_by_name(GCEnvironment *env, const char *name);
bool build_environment(cJSON *root_json, GCEnvironment *env);
void unescape_csv_json(char *dest, const char *src);
void escape_csv_json(char *dest, const char *src);
char *reference_counting_gc(GCEnvironment *env);
char *mark_and_sweep_gc(GCEnvironment *env);
char *mark_and_compact_gc(GCEnvironment *env);
void mark(HeapObject *obj);
void check_and_delete(HeapObject *obj);

int main()
{
    FILE *input_csv = fopen("gc_testcases.csv", "r");
    FILE *output_csv = fopen("23010763_answers.csv", "w");

    if (!input_csv || !output_csv)
    {
        printf("Error: Please ensure testcases.csv is in the current directory.\n");
        return 1;
    }
    // header
    fprintf(output_csv, "testcase_id,reference_counting_json,mark_sweep_json,mark_compact_json\n");
    char line[MAX_LINE_LENGTH];
    char clean_json[MAX_LINE_LENGTH];
    char rc_csv[MAX_LINE_LENGTH];
    char ms_csv[MAX_LINE_LENGTH];
    char mc_csv[MAX_LINE_LENGTH];
    // skip header
    fgets(line, sizeof(line), input_csv);

    while (fgets(line, sizeof(line), input_csv))
    {
        char *comma_ptr = strchr(line, ',');
        if (!comma_ptr)
        {
            continue;
        }

        *comma_ptr = '\0';
        char *testcase_id = line;
        char *raw_json = comma_ptr + 1;
        unescape_csv_json(clean_json, raw_json);

        cJSON *root_json = cJSON_Parse(clean_json);
        if (!root_json)
        {
            printf("Error parsing JSON for testcase %s\n", testcase_id);
            continue;
        }
        GCEnvironment env;
        if (build_environment(root_json, &env))
        {
            char *rc_result = reference_counting_gc(&env);
            char *ms_result = mark_and_sweep_gc(&env);
            char *mc_result = mark_and_compact_gc(&env);

            escape_csv_json(rc_csv, rc_result);
            escape_csv_json(ms_csv, ms_result);
            escape_csv_json(mc_csv, mc_result);
            fprintf(output_csv, "%s,%s,%s,%s\n", testcase_id, rc_csv, ms_csv, mc_csv);
            free(rc_result);
            free(ms_result);
            free(mc_result);
        }
        cJSON_Delete(root_json);
        printf("Processed Testcase: %s\n", testcase_id);
    }
    fclose(input_csv);
    fclose(output_csv);

    printf("\nSuccess! Outputs written to answers.csv file.\n");
    return 0;
}

void reset_metadata(GCEnvironment *env)
{
    for (int i = 0; i < env->object_count; i++)
    {
        env->objects[i].ref_count = 0;
        env->objects[i].marked = false;
        env->objects[i].new_address = env->objects[i].address;
    }
}

HeapObject *find_object_by_name(GCEnvironment *env, const char *name)
{
    for (int i = 0; i < env->object_count; i++)
    {
        if (strcmp(env->objects[i].ID, name) == 0)
        {
            return &env->objects[i];
        }
    }
    return NULL;
}

bool build_environment(cJSON *root_json, GCEnvironment *env)
{
    env->object_count = 0;
    env->num_roots = 0;
    cJSON *heap_json = cJSON_GetObjectItemCaseSensitive(root_json, "heap");
    if (!heap_json)
        return false;

    // Pass 1: Create all objects and store their raw string references
    cJSON *heap_node = NULL;
    cJSON_ArrayForEach(heap_node, heap_json)
    {
        HeapObject *obj = &env->objects[env->object_count++];
        strcpy(obj->ID, heap_node->string);

        cJSON *address = cJSON_GetObjectItemCaseSensitive(heap_node, "address");
        obj->address = address ? address->valueint : 0;

        obj->num_ref_names = 0;
        obj->num_references = 0;

        cJSON *references = cJSON_GetObjectItemCaseSensitive(heap_node, "references");
        cJSON *ref_item = NULL;
        if (references)
        {
            cJSON_ArrayForEach(ref_item, references)
            {
                strcpy(obj->ref_names[obj->num_ref_names++], ref_item->valuestring);
            }
        }
    }

    // Pass 2: Link the string references to actual memory pointers
    for (int i = 0; i < env->object_count; i++)
    {
        HeapObject *obj = &env->objects[i];
        for (int j = 0; j < obj->num_ref_names; j++)
        {
            HeapObject *target = find_object_by_name(env, obj->ref_names[j]);
            if (target)
            {
                obj->references[obj->num_references++] = target;
            }
        }
    }

    // Pass 3: Resolve Root pointers
    cJSON *roots_json = cJSON_GetObjectItemCaseSensitive(root_json, "roots");
    cJSON *root_item = NULL;
    if (roots_json)
    {
        cJSON_ArrayForEach(root_item, roots_json)
        {
            HeapObject *target = find_object_by_name(env, root_item->valuestring);
            if (target)
            {
                env->roots[env->num_roots++] = target;
            }
        }
    }
    return true;
}

// Unescapes the double quotes required by CSV formatting
void unescape_csv_json(char *dest, const char *src)
{
    int d = 0, len = strlen(src);
    if (len > 0 && src[len - 1] == '\n')
        len--;
    if (len > 0 && src[len - 1] == '\r')
        len--;

    int start = (src[0] == '"') ? 1 : 0;
    int end = (src[len - 1] == '"') ? len - 1 : len;

    for (int i = start; i < end; i++)
    {
        if (src[i] == '"' && src[i + 1] == '"')
        {
            dest[d++] = '"';
            i++;
        }
        else
        {
            dest[d++] = src[i];
        }
    }
    dest[d] = '\0';
}
// Re-escapes JSON quotes to save safely into the CSV
void escape_csv_json(char *dest, const char *src)
{
    int d = 0;
    dest[d++] = '"'; // Start quote
    for (int i = 0; src[i] != '\0'; i++)
    {
        if (src[i] == '"')
        {
            dest[d++] = '"'; // Double up the quote
        }
        dest[d++] = src[i];
    }
    dest[d++] = '"'; // End quote
    dest[d] = '\0';
}

char *reference_counting_gc(GCEnvironment *env)
{
    reset_metadata(env);
    for (int i = 0; i < env->num_roots; i++)
    {
        env->roots[i]->ref_count++;
    }

    for (int i = 0; i < env->object_count; i++)
    {
        for (int j = 0; j < env->objects[i].num_references; j++)
        {
            env->objects[i].references[j]->ref_count++;
        }
    }

    for (int i = 0; i < env->object_count; i++)
    {
        if (env->objects[i].ref_count == 0 && !env->objects[i].marked)
        {
            check_and_delete(&env->objects[i]);
        }
    }

    char buffer[2048];
    strcpy(buffer, "{\"remaining_objects\":[");

    bool first = true;
    for (int i = 0; i < env->object_count; i++)
    {
        if (env->objects[i].ref_count > 0)
        {
            if (!first)
            {
                strcat(buffer, ",");
            }
            strcat(buffer, "\"");
            strcat(buffer, env->objects[i].ID);
            strcat(buffer, "\"");
            first = false;
        }
    }
    strcat(buffer, "]}");
    return strdup(buffer); // returning heap copy
}

// helper for refernce counting
void check_and_delete(HeapObject *obj)
{
    if (obj->marked)
    {
        return;
    }

    if (obj->ref_count == 0)
    {
        obj->marked = true; // Mark as deleted to break cycles
        for (int i = 0; i < obj->num_references; i++)
        {
            HeapObject *temp = obj->references[i];
            if (temp->ref_count > 0)
            {
                temp->ref_count--;
            }
            check_and_delete(temp);
        }
    }
}
char *mark_and_sweep_gc(GCEnvironment *env)
{
    reset_metadata(env);

    for (int i = 0; i < env->num_roots; i++)
    {
        mark(env->roots[i]);
    }

    char buffer[2048];
    strcpy(buffer, "{\"remaining_objects\":[");
    bool first = true;
    for (int i = 0; i < env->object_count; i++)
    {
        if (env->objects[i].marked)
        {
            if (!first)
            {
                strcat(buffer, ",");
            }
            strcat(buffer, "\"");
            strcat(buffer, env->objects[i].ID);
            strcat(buffer, "\"");
            first = false;
        }
    }
    strcat(buffer, "]}");
    return strdup(buffer);
}
void mark(HeapObject *obj)
{
    if (obj->marked)
    {
        return;
    }
    obj->marked = true;
    for (int i = 0; i < obj->num_references; i++)
    {
        mark(obj->references[i]);
    }
}

char *mark_and_compact_gc(GCEnvironment *env)
{
    reset_metadata(env);

    for (int i = 0; i < env->num_roots; i++)
    {
        mark(env->roots[i]);
    }

    int current_free_address = 0;
    for (int i = 0; i < env->object_count; i++)
    {
        if (env->objects[i].marked)
        {
            env->objects[i].new_address = current_free_address;
            current_free_address++;
        }
    }

    char buffer[2048];
    strcpy(buffer, "{\"remaining_objects\":{");
    bool first = true;
    for (int i = 0; i < env->object_count; i++)
    {
        if (env->objects[i].marked)
        {
            if (!first)
            {
                strcat(buffer, ",");
            }
            strcat(buffer, "\"");
            strcat(buffer, env->objects[i].ID);
            strcat(buffer, "\": ");

            char addr_str[16];
            sprintf(addr_str, "%d", env->objects[i].new_address);
            strcat(buffer, addr_str);
            first = false;
        }
    }
    strcat(buffer, "}}");
    return strdup(buffer);
}