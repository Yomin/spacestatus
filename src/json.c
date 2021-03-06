/*
 * Copyright (c) 2013 Martin Rödel aka Yomin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "json.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int json_value(char **str, struct json *j);

void json_init(struct json *j)
{
    memset(j, 0, sizeof(struct json));
}

char* json_patch(char *str)
{
    static char *ptr;
    static char c;
    
    if(str)
    {
        c = *str;
        *str = 0;
        ptr = str;
    }
    else
        *ptr = c;
    return ptr;
}

char* json_eat(char *str, char *token)
{
    char *ptr;
    int found, inverse = 0;
    
    if(*token == '^')
    {
        inverse = 1;
        token++;
    }
    
    while(*str)
    {
        ptr = token;
        found = 0;
        while(*ptr)
        {
            if((!inverse && *str == *ptr) || (inverse && *str != *ptr))
            {
                found = 1;
                break;
            }
            ptr++;
        }
        if(!found)
            return str;
        str++;
    }
    return str;
}

void json_eat_filler(char **str)
{
    *str = json_eat(*str, " \t\n\r");
}

int json_number(char **str, struct json *j)
{
    char *ptr1 = *str, *ptr2;
    
    if(**str == '-')
        ptr1++;
    
    ptr2 = json_eat(ptr1, "1234567890");
    if(ptr2 == ptr1)
        return 0;
    if(*ptr2 == '.')
    {
        ptr2++;
        ptr1 = json_eat(ptr2, "1234567890");
        if(ptr1 == ptr2)
            return -1;
        if(*ptr1 == 'e' || *ptr1 == 'E')
        {
            ptr1++;
            if(*ptr1 == '+' || *ptr1 == '-')
                ptr1++;
            ptr2 = json_eat(ptr1, "1234567890");
            if(ptr2 == ptr1 || ptr2-2 != ptr1)
                return -1;
            json_patch(ptr2);
        }
        else
            json_patch(ptr1);
        j->v.number.d = atof(*str);
        j->type = json_type_float;
    }
    else
    {
        json_patch(ptr2);
        j->v.number.l = atol(*str);
        j->type = json_type_int;
    }
    
    *str = json_patch(0);
    return 0;
}

int json_string(char **str, struct json *j)
{
    char *ptr;
    
    if(**str != '"')
        return -1;
    (*str)++;
    ptr = strchr(*str, '"');
    if(!ptr)
        return -1;
    json_patch(ptr);
    
    j->v.string = malloc(ptr-*str+1);
    strcpy(j->v.string, *str);
    j->v.string[ptr-*str] = 0;
    j->type = json_type_string;
    
    *str = ptr+1;
    return 0;
}

void json_malloc(struct json ***value, int *len, char ***name)
{
    if(!*len)
    {
        *value = malloc(sizeof(struct json*));
        (*value)[0] = malloc(sizeof(struct json));
        memset((*value)[0], 0, sizeof(struct json));
        (*len)++;
        if(name)
        {
            *name = malloc(sizeof(char*));
            **name = 0;
        }
    }
    else
    {
        (*len)++;
        *value = realloc(*value, sizeof(struct json*)**len);
        if(name)
        {
            *name = realloc(*name, sizeof(char*)**len);
            (*name)[*len-1] = 0;
        }
        (*value)[*len-1] = malloc(sizeof(struct json));
        memset((*value)[*len-1], 0, sizeof(struct json));
    }
}

int json_array(char **str, struct json *j)
{
    if(**str != '[')
        return -1;
    (*str)++;
    
    j->type = json_type_array;
    j->v.array.len = 0;
    
    while(1)
    {
        json_eat_filler(str);
        *str = json_eat(*str, " \t");
        json_malloc(&j->v.array.value, &j->v.array.len, 0);
        if(json_value(str, j->v.array.value[j->v.array.len-1]))
            return -1;
        json_eat_filler(str);
        *str = json_eat(*str, " \t");
        if(**str == ']')
            break;
        if(**str != ',')
            return -1;
        (*str)++;
    }
    (*str)++;
    return 0;
}

int json_object(char **str, struct json *j)
{
    if(**str != '{')
        return -1;
    (*str)++;
    
    j->type = json_type_object;
    j->v.object.len = 0;
    
    while(1)
    {
        json_eat_filler(str);
        json_malloc(&j->v.object.value, &j->v.object.len, &j->v.object.name);
        if(json_string(str, j->v.object.value[j->v.object.len-1]))
            return -1;
        j->v.object.name[j->v.object.len-1] = j->v.object.value[j->v.object.len-1]->v.string;
        json_eat_filler(str);
        if(**str != ':')
            return -1;
        (*str)++;
        json_eat_filler(str);
        if(json_value(str, j->v.object.value[j->v.object.len-1]))
            return -1;
        json_eat_filler(str);
        if(**str == '}')
            break;
        if(**str != ',')
            return -1;
        (*str)++;
    }
    (*str)++;
    return 0;
}

int json_bool(char **str, struct json *j)
{
    j->type = json_type_bool;
    if(!strncmp(*str, "true", strlen("true")))
    {
        j->v.bool = true;
        *str += strlen("true");
    }
    else if(!strncmp(*str, "false", strlen("false")))
    {
        j->v.bool = false;
        *str += strlen("false");
    }
    else
        return -1;
    return 0;
}

int json_null(char **str, struct json *j)
{
    j->type = json_type_null;
    if(!strncmp(*str, "null", strlen("null")))
    {
        *str += strlen("null");
        return 0;
    }
    return -1;
}

int json_value(char **str, struct json *j)
{
    json_eat_filler(str);
    switch(**str)
    {
    case '"':
        return json_string(str, j);
    case '[':
        return json_array(str, j);
    case '{':
        return json_object(str, j);
    case 't':
    case 'f':
        return json_bool(str, j);
    case 'n':
        return json_null(str, j);
    default:
        return json_number(str, j);
    }
}

void json_free(struct json *j)
{
    int i;
    
    if(!j)
        return;
    
    switch(j->type)
    {
    case json_type_string:
        if(j->v.string)
            free(j->v.string);
        break;
    case json_type_array:
        for(i=0; i<j->v.array.len; i++)
            if(j->v.array.value[i])
            {
                json_free(j->v.array.value[i]);
                free(j->v.array.value[i]);
            }
        free(j->v.array.value);
        break;
    case json_type_object:
        for(i=0; i<j->v.object.len; i++)
        {
            if(j->v.object.name[i])
                free(j->v.object.name[i]);
            if(j->v.object.value[i])
            {
                json_free(j->v.object.value[i]);
                free(j->v.object.value[i]);
            }
        }
        free(j->v.object.name);
        free(j->v.object.value);
        break;
    case json_type_int:
    case json_type_float:
    case json_type_bool:
    case json_type_null:
    case json_type_unset:
        break;
    }
    j->type = json_type_unset;
}

int json_parse(char *str, struct json *j)
{
    int ret;
    
    if(!j)
        return -1;
    
    if(j->type != json_type_unset)
        json_free(j);
    
    if((ret = json_value(&str, j)))
        json_free(j);
    
    return ret;
}

struct json* json_getter(char *target, struct json *j)
{
    char *ptr;
    int i;
    
    if(j->type == json_type_unset)
        return 0;
    
    switch(*target)
    {
    case 0: // any
        return j;
    case '{': // object
        target++;
        if(j->type != json_type_object)
            return 0;
        if(!*target)
            return j;
        ptr = json_eat(target, "^:");
        if(target == ptr || !*ptr)
            return 0;
        json_patch(ptr);
        for(i=0; i<j->v.object.len; i++)
            if(!strcmp(j->v.object.name[i], target))
                return json_getter(ptr+1, j->v.object.value[i]);
        return 0;
    case '[': // array
        target++;
        if(j->type != json_type_array)
            return 0;
        if(!*target)
            return j;
        ptr = json_eat(target, "1234567890");
        if(target == ptr || *ptr != ':')
            return 0;
        json_patch(ptr);
        i = atoi(target);
        if(i >= j->v.array.len)
            return 0;
        return json_getter(ptr+1, j->v.array.value[i]);
    case 'b': // bool
        if(j->type == json_type_bool)
            return j;
        return 0;
    case 'n': // null
        if(j->type == json_type_array)
            return j;
        return 0;
    case 'i': // int
        if(j->type == json_type_int)
            return j;
        return 0;
    case 'f': // float
        if(j->type == json_type_float)
            return j;
        return 0;
    case 's': // string
        if(j->type == json_type_string)
            return j;
        return 0;
    default:
        return 0;
    }
}

struct json* json_get(char *target, struct json *j)
{
    char *str;
    struct json *ret;
    
    str = malloc(strlen(target)+1);
    strcpy(str, target);
    ret = json_getter(str, j);
    free(str);
    
    return ret;
}

void json_print(struct json *j)
{
    int i;
    
    switch(j->type)
    {
    case json_type_unset:
        printf("unset");
        break;
    case json_type_string:
        printf("\"%s\"", j->v.string);
        break;
    case json_type_int:
        printf("%li", j->v.number.l);
        break;
    case json_type_float:
        printf("%f", j->v.number.d);
        break;
    case json_type_object:
        printf("{\"%s\":", j->v.object.name[0]);
        json_print(j->v.object.value[0]);
        for(i=1; i<j->v.object.len; i++)
        {
            printf(", \"%s\":", j->v.object.name[i]);
            json_print(j->v.object.value[i]);
        }
        printf("}");
        break;
    case json_type_array:
        printf("[");
        json_print(j->v.array.value[0]);
        for(i=1; i<j->v.array.len; i++)
        {
            printf(",");
            json_print(j->v.array.value[i]);
        }
        printf("]");
        break;
    case json_type_bool:
        if(j->v.bool == true)
            printf("true");
        else
            printf("false");
        break;
    case json_type_null:
        printf("null");
        break;
    }
}

#ifdef TEST

int main(int argc, char *argv[])
{
    struct json j, *jp;
    
    if(json_parse(argv[1], &j))
    {
        printf("parsing failed\n");
        return -1;
    }
    
    json_print(&j);
    printf("\n");
    
    if(!(jp = json_get(argv[2], &j)))
    {
        printf("getting failed\n");
        return -1;
    }
    
    json_print(jp);
    printf("\n");
    
    json_free(&j);
    return 0;
}

#endif
