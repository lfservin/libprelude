/*****
*
* Copyright (C) 2004 Nicolas Delon <delon.nicolas@wanadoo.fr>
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

#ifndef _LIBPRELUDE_IDMEF_CRITERION_VALUE_H
#define _LIBPRELUDE_IDMEF_CRITERION_VALUE_H

typedef struct idmef_criterion_value idmef_criterion_value_t;

#include "idmef-criteria.h"
#include "idmef-value.h"


int idmef_criterion_value_new(idmef_criterion_value_t **cv);

int idmef_criterion_value_new_regex(idmef_criterion_value_t **cv, const char *regex);

int idmef_criterion_value_new_value(idmef_criterion_value_t **cv, idmef_value_t *value,
                                    idmef_criterion_operator_t operator);

int idmef_criterion_value_new_from_string(idmef_criterion_value_t **cv, idmef_path_t *path,
                                          const char *value, idmef_criterion_operator_t operator);

int idmef_criterion_value_clone(const idmef_criterion_value_t *src, idmef_criterion_value_t **dst);

void idmef_criterion_value_destroy(idmef_criterion_value_t *value);

int idmef_criterion_value_print(idmef_criterion_value_t *value, prelude_io_t *fd);

int idmef_criterion_value_to_string(idmef_criterion_value_t *value, prelude_string_t *out);

int idmef_criterion_value_match(idmef_criterion_value_t *cv, idmef_value_t *value, idmef_criterion_operator_t operator);

#endif /* _LIBPRELUDE_IDMEF_CRITERION_VALUE_H */
