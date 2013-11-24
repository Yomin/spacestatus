
#ifndef __JSON_H__
#define __JSON_H__

struct json
{
    enum json_type
    {
        json_type_unset = 0,
        json_type_object,
        json_type_array,
        json_type_int,
        json_type_float,
        json_type_bool,
        json_type_null,
        json_type_string
    } type;
    union
    {
        struct json_object
        {
            int len;
            char **name;
            struct json **value;
        } object;
        struct json_array
        {
            int len;
            struct json **value;
        } array;
        union json_number
        {
            char b;
            short s;
            int i;
            long l;
            float f;
            double d;
        } number;
        enum json_bool
        {
            false = 0,
            true = 1
        } bool;
        char *string;
    };
};

void json_init(struct json *j);
int  json_parse(char *str, struct json *j);
void json_free(struct json *j);

struct json* json_get(char *target, struct json *j);

void json_print(struct json *j);

#endif
