/*****
*
* Copyright (C) 2001-2004 Yoann Vandoorselaere <yoann@prelude-ids.org>
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <assert.h>

#include "libmissing.h"
#include "prelude-inttypes.h"
#include "prelude-message-id.h"
#include "prelude-msgbuf.h"

#include "prelude-linked-object.h"
#include "prelude-list.h"
#include "prelude-log.h"
#include "variable.h"
#include "config-engine.h"
#include "prelude-option.h"
#include "prelude-client.h"
#include "common.h"


#define DEFAULT_INSTANCE_NAME "default"


#define option_run_all 10


struct prelude_option_context {
        prelude_list_t list;
        void *data;
        char *name;
};


struct prelude_option {
        PRELUDE_LINKED_OBJECT;
        
        prelude_list_t optlist;
        prelude_option_t *parent;
        
        prelude_option_type_t type;
        prelude_option_priority_t priority;
        char shortopt;
        
        char *value;
        const char *longopt;  
        const char *description;
        prelude_option_argument_t has_arg;
        
	int (*commit)(void *context, prelude_option_t *opt);
        int (*set)(void *context, prelude_option_t *opt, const char *optarg);
        int (*get)(void *context, prelude_option_t *opt, char *ibuf, size_t size);
	int (*destroy)(void *context, prelude_option_t *opt);
        
        const char *help;
        const char *input_validation_regex;
        enum { string, integer, boolean } input_type;

        void *private_data;
        
        prelude_list_t value_list;
        prelude_list_t context_list;
};



struct cb_list {
        prelude_list_t list;
        char *arg;
        prelude_list_t children;
        prelude_option_t *option;
};


static int get_missing_options(config_t *cfg,
                               const char *filename, prelude_list_t *cblist,
                               prelude_list_t *rootlist, int *line, int depth);

/*
 * contain all option
 */
static PRELUDE_LIST_HEAD(root_optlist);


/*
 * Warning are turned on by default.
 */
static int warnings_flags = PRELUDE_OPTION_WARNING_OPTION|PRELUDE_OPTION_WARNING_ARG;



static void option_err(int flag, const char *fmt, ...) 
{
        if ( warnings_flags & flag ) {
                va_list ap;
                
                va_start(ap, fmt);
                vfprintf(stderr, fmt, ap);
                va_end(ap);
        }
}




static prelude_option_context_t *search_context(prelude_option_t *opt, const char *name)
{
        int ret;
        prelude_list_t *tmp;
        prelude_option_context_t *ptr;

        if ( ! name || ! *name )
                name = DEFAULT_INSTANCE_NAME;
        
        prelude_list_for_each(tmp, &opt->context_list) {

                ptr = prelude_list_entry(tmp, prelude_option_context_t, list);

                ret = strcasecmp(ptr->name, name);
                if ( ret == 0 )
                        return ptr;
        }

        return NULL;
}




/*
 * Search an option of a given name in the given option list.
 */
static prelude_option_t *search_option(prelude_list_t *optlist,
                                       const char *optname, prelude_option_type_t type, int walk_children) 
{
        prelude_option_t *item, *ret;
        prelude_list_t *tmp;
        
        prelude_list_for_each(tmp, optlist) {
                item = prelude_list_entry(tmp, prelude_option_t, list);
                
                if ( walk_children || (! item->longopt && ! item->shortopt) ) {
                        ret = search_option(&item->optlist, optname, type, walk_children);
                        if ( ret )
                                return ret;
                }

                if ( ! (item->type & type) )
                        continue;
                
                if ( item->longopt && strcasecmp(item->longopt, optname) == 0 )
                        return item;
                
                if ( strlen(optname) == 1 && item->shortopt == *optname )
                        return item;
        }
        
        return NULL;
}




static int is_an_argument(const char *stuff) 
{
        int len = strlen(stuff);
        
        if ( stuff[0] == '-' && (len == 2 || (len > 2 && stuff[1] == '-')) ) 
                return -1;
        
        return 0;
}



static void reorder_argv(int *argc, char **argv, int removed, int *argv_index)
{
        int i;
        
        for ( i = removed; (i + 1) < *argc; i++ ) 
                argv[i] = argv[i + 1];

        (*argc)--;
        (*argv_index)--;
}



static int check_option_optarg(prelude_list_t *optlist, int *argc,
                               char **argv, int *argv_index, char **optarg) 
{
        if ( *argv_index < *argc && is_an_argument(argv[*argv_index]) == 0 ) {
                *optarg = argv[*argv_index];
                reorder_argv(argc, argv, (*argv_index)++, argv_index);
        }
        
        return 0;
}




static int check_option_reqarg(prelude_list_t *optlist, const char *option,
                               int *argc, char **argv, int *argv_index, char **optarg) 
{        
        if ( *argv_index >= *argc || is_an_argument(argv[*argv_index]) < 0 ) {
                fprintf(stderr, "Option %s require an argument.\n", option);
                return -1;
        }
        
        *optarg = argv[*argv_index];
        reorder_argv(argc, argv, (*argv_index)++, argv_index);
        
        return 0;
}




static int check_option(prelude_list_t *optlist, prelude_option_t *option,
                        char **optarg, int *argc, char **argv, int *argv_index) 
{
        int ret;
        
        switch (option->has_arg) {
                
        case PRELUDE_OPTION_ARGUMENT_OPTIONAL:
                ret = check_option_optarg(optlist, argc, argv, argv_index, optarg);
                break;
                
        case PRELUDE_OPTION_ARGUMENT_REQUIRED:
                ret = check_option_reqarg(optlist, option->longopt, argc, argv, argv_index, optarg);
                break;

        case PRELUDE_OPTION_ARGUMENT_NONE:
                ret = 0;
                break;

        default:
                log(LOG_ERR, "Unknow value for has_arg parameter.\n");
                return -1;
        }

        return ret;
}




static char *lookup_variable_if_needed(char *optarg) 
{
        const char *val;
        char out[1024], c;
        size_t i, j, len = 0;

        if ( ! optarg )
                return NULL;

        for ( i = 0; i <= strlen(optarg) && (len + 1) < sizeof(out); i++ ) {

                if ( optarg[i] != '$' ) {
                        out[len++] = optarg[i];
                        continue;
                }
                
                /*
                 * go to the end of the word.
                 */ 
                j = i;
                while ( optarg[i] != '\0' && optarg[i] != ' ' ) i++;
                
                /*
                 * split into token.
                 */
                c = optarg[i];
                optarg[i] = '\0';
                
                val = variable_get(optarg + j + 1);
                if ( ! val ) 
                        val = optarg + j;
                
                strncpy(&out[len], val, sizeof(out) - len);
                len += strlen(val);
                optarg[i--] = c;
        }
                
        return strdup(out);
}



static int process_cfg_file(prelude_list_t *cblist, prelude_list_t *optlist, const char *filename) 
{
        config_t *cfg;
        int line = 0, ret;

        cfg = config_open(filename);
        if ( ! cfg ) {
                log(LOG_INFO, "couldn't open %s.\n", filename);
                return -1;
        }
        
        ret = get_missing_options(cfg, filename, cblist, optlist, &line, 0);
        
        config_close(cfg);

        return ret;
}




static struct cb_list *call_option_cb(prelude_list_t *cblist, prelude_option_t *option, const char *arg) 
{
        int ret, got_prev = 0;
        struct cb_list *new, *cb;
        prelude_list_t *tmp, *prev = NULL;
        
        prelude_list_for_each(tmp, cblist) {
                
                cb = prelude_list_entry(tmp, struct cb_list, list);
                
                if ( option->priority < cb->option->priority ) 
                        got_prev = 1;
                
                if ( ! got_prev )
                        prev = tmp;
                    
                if ( option->type & PRELUDE_OPTION_TYPE_ALLOW_MULTIPLE_CALL || option->type & PRELUDE_OPTION_TYPE_CONTEXT ) 
                        continue;
                
                ret = strcmp(cb->option->longopt, option->longopt);
                if ( ret == 0 ) {
                        if ( cb->arg )
                                free(cb->arg);
                        
                        cb->arg = (arg) ? strdup(arg) : NULL;

                        return cb;
                }
        }
        
        new = malloc(sizeof(*new));
        if ( ! new ) {
                log(LOG_ERR, "memory exhausted.\n");
                return NULL;
        }
        
        PRELUDE_INIT_LIST_HEAD(&new->children);
        
        new->arg = (arg) ? strdup(arg) : NULL;
        new->option = option;

        if ( option->priority == PRELUDE_OPTION_PRIORITY_LAST ) {
                prelude_list_add_tail(&new->list, cblist);
                return new;
        }
        
        if ( ! prev )
                prev = cblist;
        
        prelude_list_add(&new->list, prev);

        return new;
}



static int do_set(void **context, prelude_option_t *opt, const char *value)
{
        int ret;
        char *str;
        prelude_option_context_t *oc;
        
        if ( ! value || ! *value )
                value = DEFAULT_INSTANCE_NAME;
        
        if ( opt->type & PRELUDE_OPTION_TYPE_CONTEXT ) {
                oc = search_context(opt, value);
                if ( oc ) {
                        *context = oc->data;
                        return 0;
                }
        }
	
        ret = opt->set(*context, opt, (str = lookup_variable_if_needed(value)));
        if ( str )
                free(str);
        
        if ( opt->type & PRELUDE_OPTION_TYPE_CONTEXT ) {
                oc = search_context(opt, value);                                
                if ( ! oc )
		        return -1;
	        
                *context = oc->data;
        }
	
        return ret;
}



static int call_option_from_cb_list(void *default_context, prelude_list_t *cblist) 
{
        int ret = 0;
        struct cb_list *cb;
        prelude_list_t *tmp, *bkp;
	void *context = default_context;
        
        prelude_list_for_each_safe(tmp, bkp, cblist) {
                cb = prelude_list_entry(tmp, struct cb_list, list);
                
                if ( cb->option->set ) {
                        ret = do_set(&context, cb->option, cb->arg);                        
                        if ( ret < 0 ) 
                                return ret;
                }
                
                if ( ! prelude_list_empty(&cb->children) ) {
                                                
                        ret = call_option_from_cb_list(context, &cb->children);                        
                        if ( ret < 0 )
                                return ret;
                        
			if ( cb->option->commit ) 
				ret = cb->option->commit(context, cb->option);
                }
		
                context = default_context;
		
                if ( cb->arg )
                        free(cb->arg);
                
                prelude_list_del(&cb->list);
                free(cb);
        }
        
        return 0;
}




/*
 * Try to get all option that were not set from the command line in the config file.
 */
static int get_missing_options(config_t *cfg, const char *filename,
                               prelude_list_t *cblist, prelude_list_t *rootlist, int *line, int depth) 
{
        int ret;
        prelude_option_t *opt;
        struct cb_list *cbitem;
        char *section = NULL, *entry = NULL, *value = NULL;
        
        while ( (config_get_next(cfg, &section, &entry, &value, line)) == 0 ) {
                              
                opt = search_option(rootlist, (section && ! entry) ?
                                    section : entry, PRELUDE_OPTION_TYPE_CFG, 0);
                
                if ( ! opt && entry && value && strcmp(entry, "include") == 0 ) {        
                        ret = process_cfg_file(cblist, rootlist, value);
                        if ( ret < 0 ) 
                                return ret;
                        
                        continue;
                }

                if ( ! opt ) {
                        
                        if ( depth != 0 ) {
                                (*line)--;
                                if ( entry ) free(entry);
                                if ( value ) free(value);
                                if ( section ) free(section);
                                
                                return 0;
                        }

                        if ( section && ! entry )
                                option_err(PRELUDE_OPTION_WARNING_OPTION,
                                           "%s:%d: invalid section : \"%s\".\n", filename, *line, section);
                        else
                                option_err(PRELUDE_OPTION_WARNING_ARG,
                                           "%s:%d: invalid option \"%s\" in \"%s\" section.\n",
                                           filename, *line, entry, (section) ? section : "global");

                        continue;
                }
                        
                if ( section && ! entry ) {
                        cbitem = call_option_cb(cblist, opt, value);
                        if ( ! cbitem ) 
                                return -1;
                        
                        ret = get_missing_options(cfg, filename, &cbitem->children, &opt->optlist, line, depth + 1);
                        if ( ret < 0 )
                                return -1;
                                               
                } else {
                        cbitem = call_option_cb(cblist, opt, value);  
                        if ( ! cbitem ) 
                                return -1;
                }
        }

        return 0;
}



static int parse_argument(prelude_list_t *cb_list,
                          prelude_list_t *optlist, const char *filename,
                          int *argc, char **argv, int *argv_index, int depth)
{
        int ret;
        char *argptr;
        prelude_option_t *opt;
        const char *arg, *old;
        struct cb_list *cbitem;
        
        while ( *argv_index < *argc ) {
                
                old = arg = argv[(*argv_index)++];
                
                if ( *arg != '-' )
                        continue;
                                
                while ( *arg == '-' ) arg++;

                if ( ! isalnum((int) *arg) )
                        continue;
                     
                opt = search_option(optlist, arg, PRELUDE_OPTION_TYPE_CLI, 0);                
                if ( ! opt ) {                        
                        if ( depth ) {
                                (*argv_index)--;
                                return 0;
                        }
                        
                        option_err(PRELUDE_OPTION_WARNING_OPTION, "invalid option -- \"%s\" (%d).\n", arg, depth);
                        continue;
                }
                
                argptr = NULL;
                reorder_argv(argc, argv, *argv_index - 1, argv_index);
                
                ret = check_option(optlist, opt, &argptr, argc, argv, argv_index);
                if ( ret < 0 ) 
                        return -1;
                                                
                cbitem = call_option_cb(cb_list, opt, argptr);
                if ( ! cbitem )
                        return -1;
                
                /*
                 * If the option we just found have sub-option.
                 * Try to match the rest of our argument against them.
                 */
                if ( ! prelude_list_empty(&opt->optlist) ) {
                        
                        ret = parse_argument(&cbitem->children, &opt->optlist,
                                             filename, argc, argv, argv_index, depth + 1);

                        if ( ret < 0 )
                                return ret;
                }
        }
        
        return 0;
}




static int get_option_from_optlist(prelude_list_t *optlist, prelude_list_t *cb_list,
                                   const char *filename, int *argc, char **argv)
{
        int argv_index = 1, ret;
        
        if ( filename ) {
                ret = process_cfg_file(cb_list, optlist, filename);
                if ( ret < 0 )
                        return ret;
        }

        if ( argc ) 
                return parse_argument(cb_list, optlist, filename, argc, argv, &argv_index, 0);

        return 0;
}






/**
 * prelude_option_parse_arguments:
 * @context:
 * @option: A pointer on an option (list).
 * @filename: Pointer to the config filename.
 * @argc: Number of argument.
 * @argv: Argument list.
 *
 * prelude_option_parse_arguments(), parse the given argument and try to
 * match them against option in @option. If an option match, it's associated
 * callback function is called with the eventual option argument if any.
 *
 * Option not matched on the command line are searched in the configuration file
 * specified by @filename.
 *
 * if @option is NULL, all system option will be matched against argc, and argv.
 *
 * Returns: 0 if parsing the option succeed (including the case where one of
 * the callback returned -1 to request interruption of parsing), -1 if an error occured.
 */
int prelude_option_parse_arguments(void *context, prelude_option_t *option,
                                   const char *filename, int *argc, char **argv) 
{
        int ret, opt_index;
        prelude_list_t tmpl;
        PRELUDE_LIST_HEAD(optlist);
        PRELUDE_LIST_HEAD(cb_list);

        if ( option ) {
                tmpl.prev = option->list.prev;
                tmpl.next = option->list.next;
                
                prelude_list_add_tail(&option->list, &optlist);
                opt_index = get_option_from_optlist(&optlist, &cb_list, filename, argc, argv);

                option->list.prev = tmpl.prev;
                option->list.next = tmpl.next;
        } else
                opt_index = get_option_from_optlist(&root_optlist, &cb_list, filename, argc, argv);

        if ( opt_index < 0 )
                return -1;
        
        ret = call_option_from_cb_list(context, &cb_list);
        if ( ret < 0 )
                return ret;

        return opt_index;
}





/**
 * prelude_option_add:
 * @parent: Pointer on a parent option.
 * @type: bitfields.
 * @shortopt: Short option name.
 * @longopt: Long option name.
 * @desc: Description of the option.
 * @has_arg: Define if the option has argument.
 * @set: Callback to be called when the value for this option change.
 * @get: Callback to be called to get the value for this option.
 *
 * prelude_option_add() create a new option. The option is set to be the child
 * of @parent, unless it is NULL. In this case the option is a root option.
 *
 * The @type parameters can be set to PRELUDE_OPTION_TYPE_CLI (telling the
 * option may be searched on the command line), PRELUDE_OPTION_TYPE_CFG (telling
 * the option may be searched in the configuration file) or both.
 *
 * Returns: Pointer on the option object, or NULL if an error occured.
 */
prelude_option_t *prelude_option_add(prelude_option_t *parent, prelude_option_type_t type,
                                     char shortopt, const char *longopt, const char *desc,
                                     prelude_option_argument_t has_arg,
                                     int (*set)(void *context, prelude_option_t *opt, const char *optarg),
                                     int (*get)(void *context, prelude_option_t *opt, char *buf, size_t size)) 
{
        prelude_option_t *new;
        prelude_list_t *optlist;
        
        new = malloc(sizeof(*new));
        if ( ! new ) 
                return NULL;

        PRELUDE_INIT_LIST_HEAD(&new->optlist);
        PRELUDE_INIT_LIST_HEAD(&new->context_list);

        new->value = NULL;
	new->commit = NULL;
        new->destroy = NULL;
        new->priority = PRELUDE_OPTION_PRIORITY_NONE;
        new->type = type;
        new->has_arg = has_arg;
        new->longopt = longopt;
        new->shortopt = shortopt;
        new->description = desc;
        new->set = set;
        new->get = get;
        new->parent = parent;

        if ( parent )
                optlist = &parent->optlist;
        else 
                optlist = &root_optlist;
        
        prelude_list_add_tail(&new->list, optlist);
                
        return (prelude_option_t *) new;
}



static void send_option_msg(void *context, prelude_option_t *opt, const char *iname, prelude_msgbuf_t *msg)
{
        char value[1024] = { 0 };
        const char *name = (iname) ? iname : opt->longopt;

        prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_START, 0, NULL);
        prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_NAME, strlen(name) + 1, name);
        prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_HAS_ARG, sizeof(uint8_t), &opt->has_arg);
        prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_TYPE, sizeof(uint8_t), &opt->type);
        
        if ( opt->description )
                prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_DESC, strlen(opt->description) + 1, opt->description);

        if ( context && opt->get && opt->get(context, opt, value, sizeof(value)) == 0 && *value ) 
                prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_VALUE, strlen(value) + 1, value);
}


static void construct_option_msg(void *default_context, prelude_msgbuf_t *msg, prelude_list_t *optlist) 
{
        char value[1024];
        prelude_option_t *opt;
        prelude_list_t *tmp, *tmp2;
	prelude_option_context_t *oc;
        
        if ( prelude_list_empty(optlist) )
                return;
                
        prelude_list_for_each(tmp, optlist) {
                opt = prelude_list_entry(tmp, prelude_option_t, list);

                if ( ! (opt->type & PRELUDE_OPTION_TYPE_WIDE) )
                        continue;
                
                prelude_list_for_each(tmp2, &opt->context_list) {
                        oc = prelude_list_entry(tmp2, prelude_option_context_t, list);
                        
                        snprintf(value, sizeof(value), "%s[%s]", opt->longopt, oc->name);

                        send_option_msg(oc->data, opt, value, msg);
                        construct_option_msg(oc->data, msg, &opt->optlist);
                        prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_END, 0, NULL);
                }

		send_option_msg(default_context, opt, NULL, msg);
                construct_option_msg(NULL, msg, &opt->optlist);
                prelude_msgbuf_set(msg, PRELUDE_MSG_OPTION_END, 0, NULL);
        }
}




int prelude_option_wide_send_msg(void *context, prelude_msgbuf_t *msgbuf)
{
        prelude_msgbuf_set(msgbuf, PRELUDE_MSG_OPTION_LIST, 0, NULL);
        prelude_msgbuf_set(msgbuf, PRELUDE_MSG_OPTION_START, 0, NULL);
        
        construct_option_msg(context, msgbuf, &root_optlist);
        
        return 0;
}




static int get_max_char(const char *line, int descoff) 
{
        int desclen;
        int max = 0 , i;
        
        desclen = 80 - descoff;
        
        for ( i = 0; i < desclen; i++ ) {

                if ( line[i] == '\0' )
                        return i;
                
                if ( line[i] == ' ' )
                        max = i;
        }
        
        return max;
}



static void print_wrapped(const char *line, int descoff) 
{
        int max, i = 0, j;
        
        while ( 1 ) {
                max = get_max_char(&line[i], descoff);
                
                while ( max-- >= 0 ) {
                        
                        if ( line[i] == '\0' ) {
                                putchar('\n');
                                return;
                        } else
                                putchar(line[i++]);
                }
                        
                putchar('\n');
                for ( j = 0; j < descoff; j++ )
                        putchar(' ');
        }
}



static void print_options(prelude_list_t *optlist, prelude_option_type_t type, int descoff, int depth) 
{
        int i, separator;
        prelude_option_t *opt;
        prelude_list_t *tmp;
        
        prelude_list_for_each(tmp, optlist) {
                
                opt = prelude_list_entry(tmp, prelude_option_t, list);

                /*
                 * If type is not there, continue.
                 */
                if ( type && ! (opt->type & type) ) 
                        continue;
                
                for ( i = 0; i < depth; i++ )
                        printf("  ");
                
                if ( opt->shortopt != 0 )
                        i += printf("-%c ", opt->shortopt);

                if ( opt->longopt )
                        i += printf("--%s ", opt->longopt);
                
                while ( i++ < descoff )
                        putchar(' ');

                if ( opt->description ) {
                        print_wrapped(opt->description, depth + descoff);
                        separator = (strlen(opt->description) > (80 - descoff)) ? 1 : 0;
                        if ( separator )
                                putchar('\n');
                } else
                        putchar('\n');
                
                if ( ! prelude_list_empty(&opt->optlist) ) 
                        print_options(&opt->optlist, type, descoff, depth + 1);
        }

        putchar('\n');
}




/**
 * prelude_option_print:
 * @opt: Option(s) to print out.
 * @type: Only option with specified types will be printed.
 * @descoff: offset from the begining of the line where the description
 * should start.
 *
 * Dump option available in @opt and hooked to the given types.
 * If @opt is NULL, then the root of the option list is used.
 */
void prelude_option_print(prelude_option_t *opt, prelude_option_type_t type, int descoff) 
{
        prelude_list_t *optlist = (opt) ? &opt->optlist : &root_optlist;
        
        print_options(optlist, type, descoff, 0);
}



/**
 * prelude_option_set_priority:
 * @option: Pointer on an option object.
 * @priority: Priority of the option object.
 *
 * prelude_option_set_priority() can be used to associate a priority
 * with an option. This can be used to solve dependancies problem within
 * differents options.
 *
 * A priority of -1 mean an option will always be executed last (with
 * all other option with a -1 priority).
 *
 * The default priority for an option is 0, the caller is responssible
 * for the way it assign priority (knowing that highest priority are always
 * executed first).
 */
void prelude_option_set_priority(prelude_option_t *option, prelude_option_priority_t priority) 
{
        assert(prelude_list_empty(&option->optlist));
        option->priority = priority;
}




/**
 * prelude_option_destroy:
 * @option: Pointer on an option object.
 *
 * Destroy a #prelude_option_t object and all data associated
 * with it (including all suboption).
 */
void prelude_option_destroy(prelude_option_t *option)
{
        prelude_option_t *opt;
        prelude_list_t *tmp, *bkp, *optlist;
        
        if ( ! option )
                optlist = &root_optlist;
        else {
                optlist = &option->optlist;
                prelude_list_del(&option->list);
        }
        
        prelude_list_for_each_safe(tmp, bkp, optlist) {
                
                opt = prelude_list_entry(tmp, prelude_option_t, list);
                prelude_option_destroy(opt);
        }

        if ( option ) {
                if ( option->value )
                        free(option->value);
        
                free(option);
        }
}




/**
 * prelude_option_set_warnings;
 * @warnings: bitwise OR of flags.
 * @old_flags: Pointer to an integer where to store old flags to.
 *
 * Set current warnings flags to @flags (unless @flags is 0).
 *
 * Uppon return, the variable pointed to by @old_flags is updated
 * to contain the old flags unless it point to NULL.
 */
void prelude_option_set_warnings(prelude_option_warning_t new, prelude_option_warning_t *old_warnings) 
{
        if ( old_warnings ) 
                *old_warnings = warnings_flags;
        
        warnings_flags = new;
}


void prelude_option_set_commit_callback(prelude_option_t *opt,
     	                                int (*commit)(void *context, prelude_option_t *opt))
{
	opt->commit = commit;
}


void *prelude_option_get_commit_callback(prelude_option_t *opt)
{
	return opt->commit;
}


void prelude_option_set_get_callback(prelude_option_t *opt,
                                     int (*get)(void *context, prelude_option_t *opt, char *buf, size_t size))
{
        opt->get = get;
}



void *prelude_option_get_get_callback(prelude_option_t *opt)
{
        return opt->get;
}




void prelude_option_set_set_callback(prelude_option_t *opt,
                                     int (*set)(void *context, prelude_option_t *opt, const char *optarg))
{
        opt->set = set;
}



void *prelude_option_get_set_callback(prelude_option_t *opt)
{
        return opt->set;
}



void prelude_option_set_destroy_callback(prelude_option_t *opt,
                                         int (*destroy)(void *context, prelude_option_t *opt))
{
        opt->destroy = destroy;
        opt->type |= PRELUDE_OPTION_TYPE_DESTROY;
}



void *prelude_option_get_destroy_callback(prelude_option_t *opt)
{
        return opt->destroy;
}



char prelude_option_get_shortname(prelude_option_t *opt) 
{
        return opt->shortopt;
}



const char *prelude_option_get_longname(prelude_option_t *opt) 
{
        return opt->longopt;
}



void prelude_option_set_value(prelude_option_t *opt, const char *value)
{
        if ( opt->value )
                free(opt->value);
        
        opt->value = strdup(value);
}



prelude_list_t *prelude_option_get_optlist(prelude_option_t *opt)
{
        return &opt->optlist;
}



prelude_option_t *prelude_option_get_next(prelude_option_t *start, prelude_option_t *cur)
{
	return prelude_list_get_next(cur, &start->optlist, prelude_option_t, list);
}



int prelude_option_has_optlist(prelude_option_t *opt)
{
	return ! prelude_list_empty(&opt->optlist);
}



const char *prelude_option_get_value(prelude_option_t *opt)
{
        return opt->value;
}



void prelude_option_set_private_data(prelude_option_t *opt, void *data) 
{
        opt->private_data = data;
}



void *prelude_option_get_private_data(prelude_option_t *opt) 
{
        return opt->private_data;
}



int prelude_option_invoke_set(void **context, prelude_option_t *opt, const char *value, char *err, size_t size)
{       
        if ( ! opt->set ) {
                snprintf(err, size, "%s does not support set operation", opt->longopt);
                return -1;
        }
        
        if ( value && ! *value )
                value = NULL;
        
        if ( opt->has_arg == PRELUDE_OPTION_ARGUMENT_NONE && value != NULL ) {
                snprintf(err, size, "%s does not take an argument", opt->longopt);
                return -1;
        }
        
        if ( opt->has_arg == PRELUDE_OPTION_ARGUMENT_REQUIRED && value == NULL ) {
                snprintf(err, size, "%s require an argument", opt->longopt);
                return -1;
        }

        return do_set(context, opt, value);
}



int prelude_option_invoke_commit(void *context, prelude_option_t *opt, const char *value, char *err, size_t size)
{
	int ret;
	prelude_option_context_t *oc = NULL;
	
        if ( ! opt->commit ) 
		return 0;
        
	if ( opt->type & PRELUDE_OPTION_TYPE_CONTEXT ) {
		oc = search_context(opt, value);
		if ( ! oc ) {
			snprintf(err, size, "could not find option with context %s[%s]", opt->longopt, value);
			return -1;
		}
		context = oc->data;
	}
	
        ret = opt->commit(context, opt);
	if ( ret < 0 )
		snprintf(err, size, "error in commit callback for %s", opt->longopt);
	
	return ret;
}



int prelude_option_invoke_destroy(void *context, prelude_option_t *opt, const char *value, char *err, size_t size)
{
	int ret;
	prelude_option_context_t *oc = NULL;
	
        if ( ! opt->destroy ) {
                snprintf(err, size, "%s does not support destruction", opt->longopt);
		return -1;
	}
        
	if ( opt->type & PRELUDE_OPTION_TYPE_CONTEXT ) {
		oc = search_context(opt, value);
		if ( ! oc ) {
			snprintf(err, size, "could not find option with context %s[%s]", opt->longopt, value);
			return -1;
		}
		context = oc->data;
	}
	
        ret = opt->destroy(context, opt);
	if ( ret < 0 ) {
		snprintf(err, size, "error in the destruction callback for %s", opt->longopt);
		return -1;
	}
	
	if ( oc )
		prelude_option_context_destroy(oc);
	
	return 0;
}



int prelude_option_invoke_get(void *context, prelude_option_t *opt, const char *value, char *buf, size_t size)
{
	prelude_option_context_t *oc;
	
        if ( ! opt->get ) {
                snprintf(buf, size, "%s doesn't support value retrieval", opt->longopt);
                return -1;
        }
        
	if ( opt->type & PRELUDE_OPTION_TYPE_CONTEXT ) {
		oc = search_context(opt, value);
		if ( ! oc ) {
			snprintf(buf, size, "could not find option with context %s[%s]", opt->longopt, value);
			return -1;
		}
		
		context = oc->data;
	}
	
        return opt->get(context, opt, buf, size);
}




prelude_option_t *prelude_option_new(prelude_option_t *parent) 
{
        prelude_option_t *new;
        prelude_list_t *optlist = parent ? &parent->optlist : &root_optlist;
                
        new = calloc(1, sizeof(*new));
        if ( ! new ) 
                return NULL;

        new->parent = parent;
        PRELUDE_INIT_LIST_HEAD(&new->optlist);
        PRELUDE_INIT_LIST_HEAD(&new->context_list);
        
        prelude_list_add_tail(&new->list, optlist);

        return new;
}



void prelude_option_set_longopt(prelude_option_t *opt, const char *longopt) 
{
        opt->longopt = longopt;
}



const char *prelude_option_get_longopt(prelude_option_t *opt) 
{
        return opt->longopt;
}



void prelude_option_set_description(prelude_option_t *opt, const char *description) 
{
        opt->description = description;
}



const char *prelude_option_get_description(prelude_option_t *opt)
{
        return opt->description;
}



void prelude_option_set_type(prelude_option_t *opt, prelude_option_type_t type)
{
        opt->type = type;
}


prelude_option_type_t prelude_option_get_type(prelude_option_t *opt)
{
        return opt->type;
}



void prelude_option_set_has_arg(prelude_option_t *opt, prelude_option_argument_t has_arg) 
{
        opt->has_arg = has_arg;
}



prelude_option_argument_t prelude_option_get_has_arg(prelude_option_t *opt) 
{
        return opt->has_arg;
}



void prelude_option_set_help(prelude_option_t *opt, const char *help) 
{
        opt->help = help;
}



const char *prelude_option_get_help(prelude_option_t *opt) 
{
        return opt->help;
}



void prelude_option_set_input_validation_regex(prelude_option_t *opt, const char *regex)
{
        opt->input_validation_regex = regex;
}



const char *prelude_option_get_input_validation_regex(prelude_option_t *opt)
{
        return opt->input_validation_regex;
}



void prelude_option_set_input_type(prelude_option_t *opt, uint8_t input_type) 
{
        opt->input_type = input_type;
}



uint8_t prelude_option_get_input_type(prelude_option_t *opt)
{
        return opt->input_type;
}



prelude_option_t *prelude_option_get_parent(prelude_option_t *opt)
{
        return opt->parent;
}



prelude_option_context_t *prelude_option_new_context(prelude_option_t *opt, const char *name, void *data)
{
        prelude_option_context_t *new;
	
        new = malloc(sizeof(*new));
        if ( ! new ) {
                log(LOG_ERR, "memory exhausted.\n");
                return NULL;
        }

        if ( name && ! *name )
                name = DEFAULT_INSTANCE_NAME;
                
        new->data = data;
        new->name = (name) ? strdup(name) : NULL;
        
        if ( opt ) {
        	opt->type |= PRELUDE_OPTION_TYPE_CONTEXT;
		prelude_list_add_tail(&new->list, &opt->context_list);
        } else {         	
		PRELUDE_INIT_LIST_HEAD(&new->list);
        }
	
        return new;
}



void prelude_option_context_destroy(prelude_option_context_t *oc)
{
        if ( ! prelude_list_empty(&oc->list) )
                prelude_list_del(&oc->list);

        if ( oc->name )
                free(oc->name);

        free(oc);
}



prelude_option_t *prelude_option_search(prelude_option_t *parent, const char *name,
                                        prelude_option_type_t type, int walk_children)
{
        prelude_list_t *optlist = parent ? &parent->optlist : &root_optlist;
        return search_option(optlist, name, type, walk_children);
}