# Copyright (C) 2003,2004 Nicolas Delon <nicolas@prelude-ids.org>
# All Rights Reserved
#
# This file is part of the Prelude program.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

package GenerateIDMEFMessageReadC;

use Generate;
@ISA = qw/Generate/;

use strict;
use IDMEFTree;

sub	header
{
     my	$self = shift;

     $self->output("
/*****
*
* Copyright (C) 2001-2004 Yoann Vandoorselaere <yoann\@prelude-ids.org>
* Copyright (C) 2003 Nicolas Delon <delon.nicolas\@wanadoo.fr>
* All Rights Reserved
*
* This file is part of the Prelude program.
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

/* Auto-generated by the GenerateIDMEFMessageReadC package */

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include \"prelude-inttypes.h\"
#include \"prelude-list.h\"
#include \"prelude-log.h\"
#include \"extract.h\"
#include \"prelude-io.h\"
#include \"prelude-message.h\"
#include \"prelude-message-buffered.h\"
#include \"idmef-message-id.h\"
#include \"idmef.h\"
#include \"idmef-tree-wrap.h\"

#include \"idmef-message-read.h\"
#include \"idmef-util.h\"

#define extract_string_safe(out, buf, len) extract_string_safe_f(__FUNCTION__, __LINE__, out, buf, len)

static inline int extract_string_safe_f(const char *f, int line, prelude_string_t **out, char *buf, size_t len)
\{
        /*
         * we use len - 1 since len is supposed to include \0 to avoid making a dup.
         */

        *out = prelude_string_new_ref_fast(buf, len - 1);
        if ( ! *out ) \
                return -2;

        return 0;
\}


static inline int extract_time_safe(idmef_time_t **out, void *buf, size_t len)
\{
        /*
         * sizeof(sec) + sizeof(usec) + sizeof(gmt offset).
         */
        if ( len != 12 ) \{
                log(LOG_ERR, \"Datatype error: buffer is not a idmef time.\\n\");
                return -1;
        \}

        *out = idmef_time_new();
        if ( ! *out ) \{
                log(LOG_ERR, \"memory exhausted.\\n\");
                return -2;
        \}

        idmef_time_set_sec(*out, extract_uint32(buf));
        idmef_time_set_usec(*out, extract_uint32(buf + 4));
        idmef_time_set_gmt_offset(*out, extract_int32(buf + 8));

        return 0;
\}


static inline int extract_data_safe(idmef_data_t **out, void *buf, size_t len)
\{
        *out = idmef_data_new_ref(buf, len);
        if ( ! *out ) \{
                log(LOG_ERR, \"memory exhausted.\\n\");
                return -1;
        \}

        return 0;
\}


");
}

sub	struct_field_normal
{
    my	$self = shift;
    my	$tree = shift;
    my	$struct = shift;
    my	$field = shift;
    my	$ptr = ($field->{metatype} & (&METATYPE_STRUCT | &METATYPE_LIST)) ? "*" : "";
    my	$type = shift || $field->{value_type};
    my	$var_type = shift || "$field->{typename}";

    $self->output("
			case MSG_",  uc($struct->{short_typename}), "_", uc($field->{short_name}), ": \{
                                ${var_type} ${ptr}tmp;

				if ( extract_${type}_safe(&tmp, buf, len) < 0 )
					goto error;

				idmef_$struct->{short_typename}_set_$field->{short_name}($struct->{short_typename}, tmp);
				break;
			\}
");
}

sub	struct_field_struct
{
    my	$self = shift;
    my	$tree = shift;
    my	$struct = shift;
    my	$field = shift;
    my	$name = shift || $field->{name};

    $self->output("
			case MSG_",  uc($field->{short_typename}), "_TAG", ": \{
				$field->{typename} *tmp;

				tmp = idmef_$struct->{short_typename}_new_${name}($struct->{short_typename});
				if ( ! tmp)
					goto error;

				if ( ! idmef_$field->{short_typename}_read(tmp, msg) )
					goto error;

				break;
			\}
");
}

sub	struct_field_union
{
    my	$self = shift;
    my	$tree = shift;
    my	$struct = shift;
    my	$field = shift;

    foreach my $member ( @{$field->{member_list}} ) {
	$self->struct_field_struct($tree, $struct, $member);
    }
}

sub	struct
{
    my	$self = shift;
    my	$tree = shift;
    my	$struct = shift;

    if ( $struct->{toplevel} ) {
	$self->output("
/*
 * It is up to the caller to implement the toplevel function in order to handle
 * specific stuff
 */
");
	return;
    }

    $self->output("
$struct->{typename} *idmef_$struct->{short_typename}_read($struct->{typename} *$struct->{short_typename}, prelude_msg_t *msg)
\{
	void *buf;
	uint8_t tag;
	uint32_t len;

	while ( 1 ) \{

		if ( prelude_msg_get(msg, &tag, &len, &buf) < 0 )
			goto error;

		switch ( tag ) \{
");

    foreach my $field ( @{$struct->{field_list}} ) {

	if ( $field->{metatype} & &METATYPE_NORMAL ) {

	    if ( $field->{metatype} & &METATYPE_PRIMITIVE ) {
		$self->struct_field_normal($tree, $struct, $field);

	    } elsif ( $field->{metatype} & &METATYPE_ENUM ) {
		$self->struct_field_normal($tree, $struct, $field, "int32", "int32_t");

	    } else {
		$self->struct_field_struct($tree, $struct, $field);
	    }

	} elsif ( $field->{metatype} & &METATYPE_LIST ) {
	    if ( $field->{metatype} & &METATYPE_PRIMITIVE ) {
		$self->struct_field_normal($tree, $struct, $field);

	    } else {
		$self->struct_field_struct($tree, $struct, $field, $field->{short_name});
	    }

	} elsif ( $field->{metatype} & &METATYPE_UNION ) {
	    $self->struct_field_union($tree, $struct, $field);
	}
    }

    $self->output("
			case MSG_END_OF_TAG:
				return $struct->{short_typename};

			default:
				log(LOG_ERR, \"couldn't handle tag %d.\\n\", tag);
				goto error;
		\}

	\}

	return $struct->{short_typename};

error:
	/* idmef_$struct->{short_typename}_destroy($struct->{short_typename}); */
	return NULL;
\}
");
}

1;
