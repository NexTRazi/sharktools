/* Copyright (c) 2007-2011
 *      Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING); if not, see
 * http://www.gnu.org/licenses/, or contact Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 ****************************************************************
 */

/* IN NO EVENT SHALL MIT BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
 * SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF
 * THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF MIT HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * MIT SPECIFICALLY DISCLAIMS ANY EXPRESS OR IMPLIED WARRANTIES INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
 *
 * MIT HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 * ENHANCEMENTS, OR MODIFICATIONS TO THIS SOFTWARE.
 */

/*
 * This file adds the add_decode_as functionality to Sharktools Core
 *
 * To learn how to use it, check out the -d option in $(man tshark)
 *
 * Most of this code was copied verbatim from tshark.c, with a few minor modifications
 * to get it to compile.
 *
 * The most significant addition is the remove_decode_as() function added to the bottom,
 * which resets dissectors.
 *
 * Contact: Armen Babikyan, MIT Lincoln Laboratory, <armenb@mit.edu>
 */

#include "sharktools_add_decode_as.h"

extern char sharktools_errmsg[2048];

/*
 * This is the template for the decode as option; it is shared between the
 * various functions that output the usage for this parameter.
 */
static const gchar decode_as_arg_template[] = "<layer_type>==<selector>,<decode_as_protocol>";

/*
 * The protocol_name_search structure is used by find_protocol_name_func()
 * to pass parameters and store results
 */
struct protocol_name_search{
  gchar              *searched_name;  /* Protocol filter name we are looking for */
  dissector_handle_t  matched_handle; /* Handle for a dissector whose protocol has the specified filter name */
  guint               nb_match;       /* How many dissectors matched searched_name */
};
typedef struct protocol_name_search *protocol_name_search_t;

/*
 * This function parses all dissectors associated with a table to find the
 * one whose protocol has the specified filter name.  It is called
 * as a reference function in a call to dissector_table_foreach_handle.
 * The name we are looking for, as well as the results, are stored in the
 * protocol_name_search struct pointed to by user_data.
 * If called using dissector_table_foreach_handle, we actually parse the
 * whole list of dissectors.
 */
static void
find_protocol_name_func(const gchar *table, gpointer handle, gpointer user_data)

{
  int                         proto_id;
  const gchar                *protocol_filter_name;
  protocol_name_search_t      search_info;

  g_assert(handle);

  search_info = (protocol_name_search_t)user_data;

  proto_id = dissector_handle_get_protocol_index((dissector_handle_t)handle);
  if (proto_id != -1) {
    protocol_filter_name = proto_get_protocol_filter_name(proto_id);
    g_assert(protocol_filter_name != NULL);
    if (strcmp(protocol_filter_name, search_info->searched_name) == 0) {
      /* Found a match */
      if (search_info->nb_match == 0) {
        /* Record this handle only if this is the first match */
        search_info->matched_handle = (dissector_handle_t)handle; /* Record the handle for this matching dissector */
      }
      search_info->nb_match++;
    }
  }
}

/*
 * The function below parses the command-line parameters for the decode as
 * feature (a string pointer by cl_param).
 * It checks the format of the command-line, searches for a matching table
 * and dissector.  If a table/dissector match is not found, we display a
 * summary of the available tables/dissectors (on stderr) and return FALSE.
 * If everything is fine, we get the "Decode as" preference activated,
 * then we return TRUE.
 */
gboolean
add_decode_as(const gchar *cl_param)
{
  gchar                        *table_name;
  guint32                       selector;
  gchar                        *decoded_param;
  gchar                        *remaining_param;
  gchar                        *selector_str;
  gchar                        *dissector_str;
  dissector_handle_t            dissector_matching;
  dissector_table_t             table_matching;
  ftenum_t                      dissector_table_selector_type;
  struct protocol_name_search   user_protocol_name;

  /* The following code will allocate and copy the command-line options in a string pointed by decoded_param */

  g_assert(cl_param);
  decoded_param = g_strdup(cl_param);
  g_assert(decoded_param);


  /* The lines below will parse this string (modifying it) to extract all
    necessary information.  Note that decoded_param is still needed since
    strings are not copied - we just save pointers. */

  /* This section extracts a layer type (table_name) from decoded_param */
  table_name = decoded_param; /* Layer type string starts from beginning */

  remaining_param = strchr(table_name, '=');
  if (remaining_param == NULL) {
    sprintf(sharktools_errmsg, "Parameter \"%s\" doesn't follow the template \"%s\"", cl_param, decode_as_arg_template);
    /* If the argument does not follow the template, carry on anyway to check
       if the table name is at least correct.  If remaining_param is NULL,
       we'll exit anyway further down */
  }
  else {
    *remaining_param = '\0'; /* Terminate the layer type string (table_name) where '=' was detected */
  }

  /* Remove leading and trailing spaces from the table name */
  while ( table_name[0] == ' ' )
    table_name++;
  while ( table_name[strlen(table_name) - 1] == ' ' )
    table_name[strlen(table_name) - 1] = '\0'; /* Note: if empty string, while loop will eventually exit */

/* The following part searches a table matching with the layer type specified */
  table_matching = NULL;

/* Look for the requested table */
  if ( !(*(table_name)) ) { /* Is the table name empty, if so, don't even search for anything, display a message */
    sprintf(sharktools_errmsg, "No layer type specified"); /* Note, we don't exit here, but table_matching will remain NULL, so we exit below */
  }
  else {
    table_matching = find_dissector_table(table_name);
    if (!table_matching) {
      sprintf(sharktools_errmsg, "Unknown layer type -- %s", table_name); /* Note, we don't exit here, but table_matching will remain NULL, so we exit below */
    }
  }

  if (!table_matching) {
    sprintf(sharktools_errmsg, "Specified layer type not found");
  }
  if (remaining_param == NULL || !table_matching) {
    /* Exit if the layer type was not found, or if no '=' separator was found
       (see above) */
    g_free(decoded_param);
    return FALSE;
  }

  if (*(remaining_param + 1) != '=') { /* Check for "==" and not only '=' */
    sprintf(sharktools_errmsg, "WARNING: -d requires \"==\" instead of \"=\". Option will be treated as \"%s==%s\"", table_name, remaining_param + 1);
  }
  else {
    remaining_param++; /* Move to the second '=' */
    *remaining_param = '\0'; /* Remove the second '=' */
  }
  remaining_param++; /* Position after the layer type string */

  /* This section extracts a selector value (selector_str) from decoded_param */

  selector_str = remaining_param; /* Next part starts with the selector number */

  remaining_param = strchr(selector_str, ',');
  if (remaining_param == NULL) {
    sprintf(sharktools_errmsg, "Parameter \"%s\" doesn't follow the template \"%s\"", cl_param, decode_as_arg_template);
    /* If the argument does not follow the template, carry on anyway to check
       if the selector value is at least correct.  If remaining_param is NULL,
       we'll exit anyway further down */
  }
  else {
    *remaining_param = '\0'; /* Terminate the selector number string (selector_str) where ',' was detected */
  }

  dissector_table_selector_type = get_dissector_table_selector_type(table_name);

  switch (dissector_table_selector_type) {

  case FT_UINT8:
  case FT_UINT16:
  case FT_UINT24:
  case FT_UINT32:
    /* The selector for this table is an unsigned number.  Parse it as such.
       There's no need to remove leading and trailing spaces from the
       selector number string, because sscanf will do that for us. */
    if ( sscanf(selector_str, "%u", &selector) != 1 ) {
      sprintf(sharktools_errmsg, "Invalid selector number \"%s\"", selector_str);
      g_free(decoded_param);
      return FALSE;
    }
    break;

  case FT_STRING:
  case FT_STRINGZ:
#if !(WIRESHARK_0_99_6_OR_EARLIER || WIRESHARK_1_8_0 || WIRESHARK_1_10_0)
  case FT_EBCDIC:
#endif
    /* The selector for this table is a string. */
    break;

  default:
    /* There are currently no dissector tables with any types other
       than the ones listed above. */
    g_assert_not_reached();
  }

  if (remaining_param == NULL) {
    /* Exit if no ',' separator was found (see above) */
    sprintf(sharktools_errmsg, "no ',' separator was found");
    g_free(decoded_param);
    return FALSE;
  }

  remaining_param++; /* Position after the selector number string */

  /* This section extracts a protocol filter name (dissector_str) from decoded_param */

  dissector_str = remaining_param; /* All the rest of the string is the dissector (decode as protocol) name */

  /* Remove leading and trailing spaces from the dissector name */
  while ( dissector_str[0] == ' ' )
    dissector_str++;
  while ( dissector_str[strlen(dissector_str) - 1] == ' ' )
    dissector_str[strlen(dissector_str) - 1] = '\0'; /* Note: if empty string, while loop will eventually exit */

  dissector_matching = NULL;

  /* We now have a pointer to the handle for the requested table inside the variable table_matching */
  if ( ! (*dissector_str) ) { /* Is the dissector name empty, if so, don't even search for a matching dissector and display all dissectors found for the selected table */
    sprintf(sharktools_errmsg, "No protocol name specified"); /* Note, we don't exit here, but dissector_matching will remain NULL, so we exit below */
  }
  else {
    user_protocol_name.nb_match = 0;
    user_protocol_name.searched_name = dissector_str;
    user_protocol_name.matched_handle = NULL;

    dissector_table_foreach_handle(table_name, find_protocol_name_func, &user_protocol_name); /* Go and perform the search for this dissector in the this table's dissectors' names and shortnames */

    if (user_protocol_name.nb_match != 0) {
      dissector_matching = user_protocol_name.matched_handle;
      if (user_protocol_name.nb_match > 1) {
        sprintf(sharktools_errmsg, "WARNING: Protocol \"%s\" matched %u dissectors, first one will be used", dissector_str, user_protocol_name.nb_match);
      }
    }
    else {
      /* OK, check whether the problem is that there isn't any such
         protocol, or that there is but it's not specified as a protocol
         that's valid for that dissector table.
         Note, we don't exit here, but dissector_matching will remain NULL,
         so we exit below */
      if (proto_get_id_by_filter_name(dissector_str) == -1) {
        /* No such protocol */
        sprintf(sharktools_errmsg, "Unknown protocol -- \"%s\"", dissector_str);
      } else {
        sprintf(sharktools_errmsg, "Protocol \"%s\" isn't valid for layer type \"%s\"",
		dissector_str, table_name);
      }
    }
  }

  if (!dissector_matching) {
    sprintf(sharktools_errmsg, "No matching dissector found");
    g_free(decoded_param);
    return FALSE;
  }

/* This is the end of the code that parses the command-line options.
   All information is now stored in the variables:
   table_name
   selector
   dissector_matching
   The above variables that are strings are still pointing to areas within
   decoded_parm.  decoded_parm thus still needs to be kept allocated in
   until we stop needing these variables
   decoded_param will be deallocated at each exit point of this function */


  /* We now have a pointer to the handle for the requested dissector
     (requested protocol) inside the variable dissector_matching */
  switch (dissector_table_selector_type) {

  case FT_UINT8:
  case FT_UINT16:
  case FT_UINT24:
  case FT_UINT32:
    /* The selector for this table is an unsigned number. */
#ifdef WIRESHARK_1_10_0
    dissector_change_uint(table_name, selector, dissector_matching);
#else
    dissector_change(table_name, selector, dissector_matching);
#endif
    break;

  case FT_STRING:
  case FT_STRINGZ:
#if !(WIRESHARK_0_99_6_OR_EARLIER || WIRESHARK_1_8_0 || WIRESHARK_1_10_0)
  case FT_EBCDIC:
#endif
    /* The selector for this table is a string. */
    dissector_change_string(table_name, selector_str, dissector_matching);
    break;

  default:
    /* There are currently no dissector tables with any types other
       than the ones listed above. */
    g_assert_not_reached();
  }
  g_free(decoded_param); /* "Decode As" rule has been succesfully added */
  return TRUE;
}

/*****************************************************************************/

/*
 * The function below parses the command-line parameters for the decode as
 * feature (a string pointer by cl_param).
 * It checks the format of the command-line, searches for a matching table
 * and dissector.  If a table/dissector match is not found, we display a
 * summary of the available tables/dissectors (on stderr) and return FALSE.
 * If everything is fine, we get the "Decode as" preference activated,
 * then we return TRUE.
 *
 * AB: copy-pasted from add_decode_as, except this calls dissector_reset{,_string}
 * instead of dissector_change{,_string}, which resets the dissector's settings
 * back to the default.
 */
gboolean
remove_decode_as(const gchar *cl_param)
{
  gchar                        *table_name;
  guint32                       selector;
  gchar                        *decoded_param;
  gchar                        *remaining_param;
  gchar                        *selector_str;
  gchar                        *dissector_str;
  dissector_handle_t            dissector_matching;
  dissector_table_t             table_matching;
  ftenum_t                      dissector_table_selector_type;
  struct protocol_name_search   user_protocol_name;

  /* The following code will allocate and copy the command-line options in a string pointed by decoded_param */

  g_assert(cl_param);
  decoded_param = g_strdup(cl_param);
  g_assert(decoded_param);


  /* The lines below will parse this string (modifying it) to extract all
    necessary information.  Note that decoded_param is still needed since
    strings are not copied - we just save pointers. */

  /* This section extracts a layer type (table_name) from decoded_param */
  table_name = decoded_param; /* Layer type string starts from beginning */

  remaining_param = strchr(table_name, '=');
  if (remaining_param == NULL) {
    sprintf(sharktools_errmsg, "Parameter \"%s\" doesn't follow the template \"%s\"", cl_param, decode_as_arg_template);
    /* If the argument does not follow the template, carry on anyway to check
       if the table name is at least correct.  If remaining_param is NULL,
       we'll exit anyway further down */
  }
  else {
    *remaining_param = '\0'; /* Terminate the layer type string (table_name) where '=' was detected */
  }

  /* Remove leading and trailing spaces from the table name */
  while ( table_name[0] == ' ' )
    table_name++;
  while ( table_name[strlen(table_name) - 1] == ' ' )
    table_name[strlen(table_name) - 1] = '\0'; /* Note: if empty string, while loop will eventually exit */

/* The following part searches a table matching with the layer type specified */
  table_matching = NULL;

/* Look for the requested table */
  if ( !(*(table_name)) ) { /* Is the table name empty, if so, don't even search for anything, display a message */
    sprintf(sharktools_errmsg, "No layer type specified"); /* Note, we don't exit here, but table_matching will remain NULL, so we exit below */
  }
  else {
    table_matching = find_dissector_table(table_name);
    if (!table_matching) {
      sprintf(sharktools_errmsg, "Unknown layer type -- %s", table_name); /* Note, we don't exit here, but table_matching will remain NULL, so we exit below */
    }
  }

  if (!table_matching) {
    sprintf(sharktools_errmsg, "Specified layer type not found");
  }
  if (remaining_param == NULL || !table_matching) {
    /* Exit if the layer type was not found, or if no '=' separator was found
       (see above) */
    g_free(decoded_param);
    return FALSE;
  }

  if (*(remaining_param + 1) != '=') { /* Check for "==" and not only '=' */
    sprintf(sharktools_errmsg, "WARNING: -d requires \"==\" instead of \"=\". Option will be treated as \"%s==%s\"", table_name, remaining_param + 1);
  }
  else {
    remaining_param++; /* Move to the second '=' */
    *remaining_param = '\0'; /* Remove the second '=' */
  }
  remaining_param++; /* Position after the layer type string */

  /* This section extracts a selector value (selector_str) from decoded_param */

  selector_str = remaining_param; /* Next part starts with the selector number */

  remaining_param = strchr(selector_str, ',');
  if (remaining_param == NULL) {
    sprintf(sharktools_errmsg, "Parameter \"%s\" doesn't follow the template \"%s\"", cl_param, decode_as_arg_template);
    /* If the argument does not follow the template, carry on anyway to check
       if the selector value is at least correct.  If remaining_param is NULL,
       we'll exit anyway further down */
  }
  else {
    *remaining_param = '\0'; /* Terminate the selector number string (selector_str) where ',' was detected */
  }

  dissector_table_selector_type = get_dissector_table_selector_type(table_name);

  switch (dissector_table_selector_type) {

  case FT_UINT8:
  case FT_UINT16:
  case FT_UINT24:
  case FT_UINT32:
    /* The selector for this table is an unsigned number.  Parse it as such.
       There's no need to remove leading and trailing spaces from the
       selector number string, because sscanf will do that for us. */
    if ( sscanf(selector_str, "%u", &selector) != 1 ) {
      sprintf(sharktools_errmsg, "Invalid selector number \"%s\"", selector_str);
      g_free(decoded_param);
      return FALSE;
    }
    break;

  case FT_STRING:
  case FT_STRINGZ:
#if !(WIRESHARK_0_99_6_OR_EARLIER || WIRESHARK_1_8_0 || WIRESHARK_1_10_0)
  case FT_EBCDIC:
#endif
    /* The selector for this table is a string. */
    break;

  default:
    /* There are currently no dissector tables with any types other
       than the ones listed above. */
    g_assert_not_reached();
  }

  if (remaining_param == NULL) {
    /* Exit if no ',' separator was found (see above) */
    sprintf(sharktools_errmsg, "no ',' separator was found");
    g_free(decoded_param);
    return FALSE;
  }

  remaining_param++; /* Position after the selector number string */

  /* This section extracts a protocol filter name (dissector_str) from decoded_param */

  dissector_str = remaining_param; /* All the rest of the string is the dissector (decode as protocol) name */

  /* Remove leading and trailing spaces from the dissector name */
  while ( dissector_str[0] == ' ' )
    dissector_str++;
  while ( dissector_str[strlen(dissector_str) - 1] == ' ' )
    dissector_str[strlen(dissector_str) - 1] = '\0'; /* Note: if empty string, while loop will eventually exit */

  dissector_matching = NULL;

  /* We now have a pointer to the handle for the requested table inside the variable table_matching */
  if ( ! (*dissector_str) ) { /* Is the dissector name empty, if so, don't even search for a matching dissector and display all dissectors found for the selected table */
    sprintf(sharktools_errmsg, "No protocol name specified"); /* Note, we don't exit here, but dissector_matching will remain NULL, so we exit below */
  }
  else {
    user_protocol_name.nb_match = 0;
    user_protocol_name.searched_name = dissector_str;
    user_protocol_name.matched_handle = NULL;

    dissector_table_foreach_handle(table_name, find_protocol_name_func, &user_protocol_name); /* Go and perform the search for this dissector in the this table's dissectors' names and shortnames */

    if (user_protocol_name.nb_match != 0) {
      dissector_matching = user_protocol_name.matched_handle;
      if (user_protocol_name.nb_match > 1) {
        sprintf(sharktools_errmsg, "WARNING: Protocol \"%s\" matched %u dissectors, first one will be used", dissector_str, user_protocol_name.nb_match);
      }
    }
    else {
      /* OK, check whether the problem is that there isn't any such
         protocol, or that there is but it's not specified as a protocol
         that's valid for that dissector table.
         Note, we don't exit here, but dissector_matching will remain NULL,
         so we exit below */
      if (proto_get_id_by_filter_name(dissector_str) == -1) {
        /* No such protocol */
        sprintf(sharktools_errmsg, "Unknown protocol -- \"%s\"", dissector_str);
      } else {
        sprintf(sharktools_errmsg, "Protocol \"%s\" isn't valid for layer type \"%s\"",
		dissector_str, table_name);
      }
    }
  }

  if (!dissector_matching) {
    sprintf(sharktools_errmsg, "No matching dissector found");
    g_free(decoded_param);
    return FALSE;
  }

/* This is the end of the code that parses the command-line options.
   All information is now stored in the variables:
   table_name
   selector
   dissector_matching
   The above variables that are strings are still pointing to areas within
   decoded_parm.  decoded_parm thus still needs to be kept allocated in
   until we stop needing these variables
   decoded_param will be deallocated at each exit point of this function */


  /* We now have a pointer to the handle for the requested dissector
     (requested protocol) inside the variable dissector_matching */
  switch (dissector_table_selector_type) {

  case FT_UINT8:
  case FT_UINT16:
  case FT_UINT24:
  case FT_UINT32:
    /* The selector for this table is an unsigned number. */
#ifdef WIRESHARK_1_10_0
    dissector_reset_uint(table_name, selector);
#else
    dissector_reset(table_name, selector);
#endif
    break;

  case FT_STRING:
  case FT_STRINGZ:
#if !(WIRESHARK_0_99_6_OR_EARLIER || WIRESHARK_1_8_0 || WIRESHARK_1_10_0)
  case FT_EBCDIC:
#endif
    /* The selector for this table is a string. */
    dissector_reset_string(table_name, selector_str);
    break;

  default:
    /* There are currently no dissector tables with any types other
       than the ones listed above. */
    g_assert_not_reached();
  }
  g_free(decoded_param); /* "Decode As" rule has been succesfully added */
  return TRUE;
}
