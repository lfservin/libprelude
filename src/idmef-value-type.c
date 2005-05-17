/*****
*
* Copyright (C) 2003, 2004, 2005 PreludeIDS Technologies. All Rights Reserved.
* Author: Yoann Vandoorselaere <yoann.v@prelude-ids.com>
*
* This file is part of the Prelude library.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by 
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "libmissing.h"
#include "prelude-inttypes.h"
#include "prelude-string.h"

#define PRELUDE_ERROR_SOURCE_DEFAULT PRELUDE_ERROR_SOURCE_IDMEF_VALUE_TYPE
#include "prelude-error.h"
#include "prelude-inttypes.h"

#include "idmef-time.h"
#include "idmef-data.h"
#include "idmef-value-type.h"


#define OBJECT_OPERATOR  IDMEF_CRITERION_OPERATOR_IS_NULL|IDMEF_CRITERION_OPERATOR_IS_NOT_NULL

#define DATA_OPERATOR    IDMEF_CRITERION_OPERATOR_EQUAL|IDMEF_CRITERION_OPERATOR_NOT_EQUAL| \
                         IDMEF_CRITERION_OPERATOR_LESSER|IDMEF_CRITERION_OPERATOR_GREATER

#define TIME_OPERATOR    IDMEF_CRITERION_OPERATOR_LESSER|IDMEF_CRITERION_OPERATOR_GREATER| \
                         IDMEF_CRITERION_OPERATOR_EQUAL|IDMEF_CRITERION_OPERATOR_NOT_EQUAL

#define STRING_OPERATOR  IDMEF_CRITERION_OPERATOR_SUBSTR|IDMEF_CRITERION_OPERATOR_EQUAL| \
                         IDMEF_CRITERION_OPERATOR_NOT_EQUAL

#define INTEGER_OPERATOR IDMEF_CRITERION_OPERATOR_LESSER|IDMEF_CRITERION_OPERATOR_GREATER|\
                         IDMEF_CRITERION_OPERATOR_EQUAL|IDMEF_CRITERION_OPERATOR_NOT_EQUAL


#define GENERIC_ONE_BASE_RW_FUNC(scanfmt, printfmt, name, type)                          \
        static int name ## _read(idmef_value_type_t *dst, const char *buf)               \
        {                                                                                \
                return sscanf(buf, (scanfmt), &(dst)->data. name ##_val) == 1 ? 0 : -1;  \
        }                                                                                \
                                                                                         \
        static int name ## _write(const idmef_value_type_t *src, prelude_string_t *out)  \
        {                                                                                \
                return prelude_string_sprintf(out, (printfmt), src->data.name ##_val);   \
        }


#define GENERIC_TWO_BASES_RW_FUNC(fmt_dec, fmt_hex, name, type)				\
        static int name ## _read(idmef_value_type_t *dst, const char *buf)		\
        {                                                                               \
                int ret;                                                                \
                                                                                        \
		if ( strncasecmp(buf, "0x", 2) == 0 )                                   \
                        ret = sscanf(buf, (fmt_hex), &(dst)->data. name ##_val);        \
		else                                                                    \
		        ret = sscanf(buf, (fmt_dec), &(dst)->data. name ##_val);        \
                                                                                        \
                return (ret <= 0) ? -1 : 0;                                             \
	}										\
											\
        static int name ## _write(const idmef_value_type_t *src, prelude_string_t *out)	\
        {										\
                return prelude_string_sprintf(out, (fmt_dec), src->data.name ##_val);	\
        }



typedef struct {
        size_t len;

        idmef_criterion_operator_t operator;
        
        int (*copy)(const idmef_value_type_t *src, void *dst, size_t size);
        int (*clone)(const idmef_value_type_t *src, idmef_value_type_t *dst, size_t size);
        
        void (*destroy)(idmef_value_type_t *type);
        int (*compare)(const idmef_value_type_t *t1, const idmef_value_type_t *t2, size_t size, idmef_criterion_operator_t op);
        
        int (*read)(idmef_value_type_t *dst, const char *buf);
        int (*write)(const idmef_value_type_t *src, prelude_string_t *out);

} idmef_value_type_operation_t;



static int byte_read(idmef_value_type_t *dst, const char *buf, unsigned int min, unsigned int max)
{
        char *endptr;
        long int tmp;

        tmp = strtol(buf, &endptr, 0);
        if ( tmp < min || tmp > max )
                return -1;
        
        dst->data.int8_val = (int8_t) tmp;

        return 0;
}


static int int8_write(const idmef_value_type_t *src, prelude_string_t *out)
{
        return prelude_string_sprintf(out, "%d", (int) src->data.int8_val);
}

static int uint8_write(const idmef_value_type_t *src, prelude_string_t *out)
{
        return prelude_string_sprintf(out, "%u", (int) src->data.int8_val);
}

static int int8_read(idmef_value_type_t *dst, const char *buf)
{
        return byte_read(dst, buf, PRELUDE_INT8_MIN, PRELUDE_INT8_MAX);
}

static int uint8_read(idmef_value_type_t *dst, const char *buf)
{
        return byte_read(dst, buf, 0, PRELUDE_UINT8_MAX);
}



GENERIC_TWO_BASES_RW_FUNC("%hd", "%hx", int16, int16_t)
GENERIC_TWO_BASES_RW_FUNC("%hu", "%hx", uint16, uint16_t)
GENERIC_TWO_BASES_RW_FUNC("%d", "%x", int32, int32_t)
GENERIC_TWO_BASES_RW_FUNC("%u", "%x", uint32, uint32_t)
GENERIC_TWO_BASES_RW_FUNC("%" PRELUDE_PRId64, "%" PRELUDE_PRIx64, int64, int64_t)
GENERIC_TWO_BASES_RW_FUNC("%" PRELUDE_PRIu64, "%" PRELUDE_PRIx64, uint64, uint64_t)

GENERIC_ONE_BASE_RW_FUNC("%d", "%d", enum, enum);
GENERIC_ONE_BASE_RW_FUNC("%f", "%f", float, float)
GENERIC_ONE_BASE_RW_FUNC("%lf", "%f", double, double)




/*
 * generic functions.
 */
static int generic_copy(const idmef_value_type_t *src, void *dst, size_t size)
{
        memcpy(dst, &src->data, size);
        return 0;
}




static int generic_clone(const idmef_value_type_t *src, idmef_value_type_t *dst, size_t size)
{
        memcpy(&dst->data, &src->data, size);
        return 0;
}



static int generic_compare(const idmef_value_type_t *t1, const idmef_value_type_t *t2,
                           size_t size, idmef_criterion_operator_t op)
{
        int ret;

        ret = memcmp(&t1->data, &t2->data, size);
        
        if ( ret == 0 && op & IDMEF_CRITERION_OPERATOR_EQUAL ) 
                return 0;

        else if ( ret < 0 && op & (IDMEF_CRITERION_OPERATOR_NOT_EQUAL|IDMEF_CRITERION_OPERATOR_LESSER) )
                return 0;

        else if ( ret > 0 && op & (IDMEF_CRITERION_OPERATOR_NOT_EQUAL|IDMEF_CRITERION_OPERATOR_GREATER) )
                return 0;

        return -1;
}




/*
 * time specific function.
 */
static int time_compare(const idmef_value_type_t *t1, const idmef_value_type_t *t2,
                        size_t size, idmef_criterion_operator_t op)
{
	double time1 = idmef_time_get_sec(t1->data.time_val) + idmef_time_get_usec(t1->data.time_val) * 1e-6;
	double time2 = idmef_time_get_sec(t2->data.time_val) + idmef_time_get_usec(t2->data.time_val) * 1e-6;

        if ( op & IDMEF_CRITERION_OPERATOR_EQUAL && time1 == time2 )
                return 0;

        else if ( op & (IDMEF_CRITERION_OPERATOR_NOT_EQUAL|IDMEF_CRITERION_OPERATOR_LESSER) && time1 < time2 )
                return 0;

        else if ( op & (IDMEF_CRITERION_OPERATOR_NOT_EQUAL|IDMEF_CRITERION_OPERATOR_GREATER) && time1 > time2 )
                return 0;

        return -1;
}



static int time_read(idmef_value_type_t *dst, const char *buf)
{
        int ret;
        
        ret = idmef_time_new_from_ntpstamp(&dst->data.time_val, buf);
        if ( ret == 0 )
                return 0;
        
	ret = idmef_time_new_from_string(&dst->data.time_val, buf);
        if ( ret == 0 )
                return 0;
        
        return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_PARSE);
}



static int time_write(const idmef_value_type_t *src, prelude_string_t *out)
{
	return idmef_time_to_string(src->data.time_val, out);
}



static int time_copy(const idmef_value_type_t *src, void *dst, size_t size)
{
        return idmef_time_copy(src->data.time_val, dst);
}



static int time_clone(const idmef_value_type_t *src, idmef_value_type_t *dst, size_t size)
{
        return idmef_time_clone(src->data.time_val, &dst->data.time_val);
}



static void time_destroy(idmef_value_type_t *type)
{
	idmef_time_destroy(type->data.time_val);
}



/*
 *
 */
static int string_compare(const idmef_value_type_t *t1, const idmef_value_type_t *t2,
                          size_t size, idmef_criterion_operator_t op)
{
        const char *s1, *s2;
        
        s1 = prelude_string_get_string(t1->data.string_val);
        s2 = prelude_string_get_string(t2->data.string_val);
        
        if ( op & IDMEF_CRITERION_OPERATOR_EQUAL && strcmp(s1, s2) == 0 )
                return 0;

        else if ( op & IDMEF_CRITERION_OPERATOR_NOT_EQUAL && strcmp(s1, s2) != 0 )
                return 0;
        
        else if ( op & IDMEF_CRITERION_OPERATOR_SUBSTR && strstr(s1, s2) )
                return 0;
        
        return -1;
}



static int string_read(idmef_value_type_t *dst, const char *buf)
{        
        return prelude_string_new_dup(&dst->data.string_val, buf);
}



static int string_copy(const idmef_value_type_t *src, void *dst, size_t size)
{
        return prelude_string_copy_dup(src->data.string_val, dst);
}



static int string_clone(const idmef_value_type_t *src, idmef_value_type_t *dst, size_t size)
{        
        return prelude_string_clone(src->data.string_val, &dst->data.string_val);
}


static void string_destroy(idmef_value_type_t *type)
{        
        prelude_string_destroy(type->data.string_val);
}



static int string_write(const idmef_value_type_t *src, prelude_string_t *out)
{
        return prelude_string_sprintf(out, "%s",
                                      prelude_string_get_string(src->data.string_val));
}



/*
 * data specific functions
 */
static int data_compare(const idmef_value_type_t *t1, const idmef_value_type_t *t2,
                        size_t len, idmef_criterion_operator_t op)
{
        int ret;
        size_t len1, len2;
        idmef_data_t *d1 = t1->data.data_val, *d2 = t2->data.data_val;
        
        len1 = idmef_data_get_len(d1);
        len2 = idmef_data_get_len(d2);

        if ( len1 == len2 )
                ret = memcmp(idmef_data_get_data(d1), idmef_data_get_data(d2), len2);
        else 
                ret = (len1 > len2) ? 1 : -1;   
        
        if ( ret == 0 && op & IDMEF_CRITERION_OPERATOR_EQUAL )
                return 0;

        else if ( ret < 0 && op & (IDMEF_CRITERION_OPERATOR_NOT_EQUAL|IDMEF_CRITERION_OPERATOR_LESSER) )
                return 0;

        else if ( ret > 0 && op & (IDMEF_CRITERION_OPERATOR_NOT_EQUAL|IDMEF_CRITERION_OPERATOR_GREATER) )
                return 0;
                                
        return -1;
}



static int data_read(idmef_value_type_t *dst, const char *src)
{
        return idmef_data_new_char_string_dup_fast(&dst->data.data_val, src, strlen(src));
}



static int data_write(const idmef_value_type_t *src, prelude_string_t *out)
{
        return idmef_data_to_string(src->data.data_val, out);
}



static int data_copy(const idmef_value_type_t *src, void *dst, size_t size)
{
        return idmef_data_copy_dup(src->data.data_val, dst);
}



static int data_clone(const idmef_value_type_t *src, idmef_value_type_t *dst, size_t size)
{
        return idmef_data_clone(src->data.data_val, &dst->data.data_val);
}



static void data_destroy(idmef_value_type_t *type)
{
        idmef_data_destroy(type->data.data_val);
}



static const idmef_value_type_operation_t ops_tbl[] = {
        { 0, 0, NULL, NULL, NULL, NULL, NULL, NULL},
        { sizeof(int8_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, int8_read, int8_write             },
        { sizeof(uint8_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, uint8_read, uint8_write           },
        { sizeof(int16_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, int16_read, int16_write           },
        { sizeof(uint16_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, uint16_read, uint16_write         },
        { sizeof(int32_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, int32_read, int32_write           },
        { sizeof(uint32_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, uint32_read, uint32_write         },
        { sizeof(int64_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, int64_read, int64_write           },
        { sizeof(uint64_t), INTEGER_OPERATOR, generic_copy, 
          generic_clone, NULL, generic_compare, uint64_read, uint64_write         },
        { sizeof(float), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, float_read, float_write           },
        { sizeof(double), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, double_read, double_write         },
        { 0, STRING_OPERATOR, string_copy,
          string_clone, string_destroy, string_compare, string_read, string_write },
        { 0, TIME_OPERATOR, time_copy,
          time_clone, time_destroy, time_compare, time_read, time_write           }, 
        { 0, DATA_OPERATOR, data_copy,
          data_clone, data_destroy, data_compare, data_read, data_write           },
        { sizeof(idmef_value_type_id_t), INTEGER_OPERATOR, generic_copy,
          generic_clone, NULL, generic_compare, enum_read, enum_write, /* enum */ },
        { 0, 0, NULL, NULL, NULL, NULL, NULL, NULL                                },
        { 0, OBJECT_OPERATOR, NULL, NULL, NULL, NULL, NULL, NULL                  },
};



static int is_type_valid(idmef_value_type_id_t type) 
{
        if ( type < 0 || type >= (sizeof(ops_tbl) / sizeof(*ops_tbl)) )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_UNKNOWN);
        
        return 0;
}




int idmef_value_type_clone(const idmef_value_type_t *src, idmef_value_type_t *dst)
{
        int ret;
        
        assert(dst->id == src->id);
        
        ret = is_type_valid(dst->id);
        if ( ret < 0 )
                return ret;

        if ( ! ops_tbl[dst->id].clone )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_CLONE_UNAVAILABLE);

        return ops_tbl[dst->id].clone(src, dst, ops_tbl[dst->id].len);
}




int idmef_value_type_copy(const idmef_value_type_t *src, void *dst)
{
        int ret;

        ret = is_type_valid(src->id);
        if ( ret < 0 )
                return ret;

        if ( ! ops_tbl[src->id].copy )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_COPY_UNAVAILABLE);
        
        return ops_tbl[src->id].copy(src, dst, ops_tbl[src->id].len);
}




int idmef_value_type_compare(const idmef_value_type_t *type1,
                             const idmef_value_type_t *type2,
                             idmef_criterion_operator_t op)
{
        int ret;

        if ( type1->id != type2->id )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_COMPARE_MISMATCH);
                        
        ret = is_type_valid(type1->id);
        if ( ret < 0 )
                return ret;

        assert(op & ops_tbl[type1->id].operator);
        
        if ( ! ops_tbl[type1->id].compare )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_COMPARE_UNAVAILABLE);
        
        return ops_tbl[type1->id].compare(type1, type2, ops_tbl[type1->id].len, op);
}




int idmef_value_type_read(idmef_value_type_t *dst, const char *buf) 
{
        int ret;
        
        ret = is_type_valid(dst->id);
        if ( ret < 0 ) 
                return ret;

        if ( ! ops_tbl[dst->id].read )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_READ_UNAVAILABLE);

        ret = ops_tbl[dst->id].read(dst, buf);
        
        return (ret < 0) ? prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_PARSE) : 0;
}




int idmef_value_type_write(const idmef_value_type_t *src, prelude_string_t *out)
{
        int ret;
        
        ret = is_type_valid(src->id);
        if ( ret < 0 )
                return ret;

        if ( ! ops_tbl[src->id].write )
                return prelude_error(PRELUDE_ERROR_IDMEF_VALUE_TYPE_WRITE_UNAVAILABLE);
        
        return ops_tbl[src->id].write(src, out);
}



void idmef_value_type_destroy(idmef_value_type_t *type)
{
        int ret;
        
        ret = is_type_valid(type->id);
        if ( ret < 0 )
                return;

        if ( ! ops_tbl[type->id].destroy )
                return;
        
        ops_tbl[type->id].destroy(type);
}




int idmef_value_type_check_operator(const idmef_value_type_t *type, idmef_criterion_operator_t op)
{
        int ret;
        
        ret = is_type_valid(type->id);
        if ( ret < 0 )
                return ret;
        
        if ( (~ops_tbl[type->id].operator & op) == 0 )
                return 0;
        
        return prelude_error(PRELUDE_ERROR_IDMEF_CRITERION_UNSUPPORTED_OPERATOR);
}
