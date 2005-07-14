#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pkg.h"
#include "parse.h"

#include "popt.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static int want_debug_spew = 0;
static int want_verbose_errors = 0;

void
debug_spew (const char *format, ...)
{
  va_list args;
  gchar *str;

  g_return_if_fail (format != NULL);
  
  if (!want_debug_spew)
    return;
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs (str, stderr);
  fflush (stdout);
  
  g_free (str);
}

void
verbose_error (const char *format, ...)
{
  va_list args;
  gchar *str;

  g_return_if_fail (format != NULL);
  
  if (!want_verbose_errors)
    return;
  
  va_start (args, format);
  str = g_strdup_vprintf (format, args);
  va_end (args);

  fputs (str, stderr);
  fflush (stdout);
  
  g_free (str);
}

#define DEFINE_VARIABLE 1

static void
popt_callback (poptContext con, 
               enum poptCallbackReason reason,
               const struct poptOption * opt,
               const char * arg, void * data)
{
  if (opt->val == DEFINE_VARIABLE)
    {
      char *varname;
      char *varval;
      char *tmp;

      tmp = g_strdup (arg);

      varname = tmp;
      while (*varname && isspace (*varname))
        ++varname;

      varval = varname;
      while (*varval && *varval != '=' && *varval != ' ')
        ++varval;

      while (*varval && (*varval == '=' || *varval == ' '))
        {
          *varval = '\0';
          ++varval;
        }

      if (*varval == '\0')
        {
          fprintf (stderr, "--define-variable argument does not have a value for the variable\n");

          exit (1);
        }

      define_global_variable (varname, varval);

      g_free (tmp);
    }
}

static gboolean
pkg_uninstalled (Package *pkg)
{
  /* See if > 0 pkgs were uninstalled */
  GSList *tmp;

  if (pkg->uninstalled)
    return TRUE;
  
  tmp = pkg->requires;
  while (tmp != NULL)
    {
      Package *pkg = tmp->data;
      
      if (pkg_uninstalled (pkg))
        return TRUE;
      
      tmp = g_slist_next (tmp);
    }

  return FALSE;
}

int 
main (int argc, char **argv)
{
  static int want_my_version = 0;
  static int want_version = 0;
  static int want_libs = 0;
  static int want_cflags = 0;
  static int want_l_libs = 0;
  static int want_L_libs = 0;
  static int want_I_cflags = 0;
  static int want_list = 0;
  static int result;
  static int want_uninstalled = 0;
  static char *variable_name = NULL;
  static int want_exists = 0;
  static char *required_atleast_version = NULL;
  static char *required_exact_version = NULL;
  static char *required_max_version = NULL;
  static char *required_pkgconfig_version = NULL;
  static int want_silence_errors = 0;
  GString *str;
  GSList *packages = NULL;
  char *search_path;
  char *pcbuilddir;
  gboolean need_newline;
  
  const char *pkgname;
  Package *pkg;

  poptContext opt_context;
  
  struct poptOption options_table[] = {
    { NULL, 0, POPT_ARG_CALLBACK, popt_callback, 0, NULL, NULL },
    { "version", 0, POPT_ARG_NONE, &want_my_version, 0,
      "output version of pkg-config" },
    { "modversion", 0, POPT_ARG_NONE, &want_version, 0,
      "output version for package" },
    { "atleast-pkgconfig-version", 0, POPT_ARG_STRING, &required_pkgconfig_version, 0,
      "require given version of pkg-config", "VERSION" },
    { "libs", 0, POPT_ARG_NONE, &want_libs, 0,
      "output all linker flags" },
    { "libs-only-l", 0, POPT_ARG_NONE, &want_l_libs, 0,
      "output -l flags" },
    { "libs-only-L", 0, POPT_ARG_NONE, &want_L_libs, 0,
      "output -L flags" },
    { "cflags", 0, POPT_ARG_NONE, &want_cflags, 0,
      "output all pre-processor and compiler flags" },
    { "cflags-only-I", 0, POPT_ARG_NONE, &want_I_cflags, 0,
      "output -I flags" },
    { "variable", 0, POPT_ARG_STRING, &variable_name, 0,
      "get the value of a variable", "VARIABLENAME" },
    { "define-variable", 0, POPT_ARG_STRING, NULL, DEFINE_VARIABLE,
      "set the value of a variable", "VARIABLENAME=VARIABLEVALUE" },
    { "exists", 0, POPT_ARG_NONE, &want_exists, 0,
      "return 0 if the module(s) exist" },
    { "uninstalled", 0, POPT_ARG_NONE, &want_uninstalled, 0,
      "return 0 if the uninstalled version of one or more module(s) or their dependencies will be used" },
    { "atleast-version", 0, POPT_ARG_STRING, &required_atleast_version, 0,
      "return 0 if the module is at least version VERSION", "VERSION" },
    { "exact-version", 0, POPT_ARG_STRING, &required_exact_version, 0,
      "return 0 if the module is at exactly version VERSION", "VERSION" },
    { "max-version", 0, POPT_ARG_STRING, &required_max_version, 0,
      "return 0 if the module is at no newer than version VERSION", "VERSION" },
    { "list-all", 0, POPT_ARG_NONE, &want_list, 0,
      "list all known packages" },
    { "debug", 0, POPT_ARG_NONE, &want_debug_spew, 0,
      "show verbose debug information" },
    { "print-errors", 0, POPT_ARG_NONE, &want_verbose_errors, 0,
      "show verbose information about missing or conflicting packages" },
    { "silence-errors", 0, POPT_ARG_NONE, &want_silence_errors, 0,
      "show verbose information about missing or conflicting packages" },
    POPT_AUTOHELP
    { NULL, 0, 0, NULL, 0 }
  };

  /* This is here so that we get debug spew from the start,
   * during arg parsing
   */
  if (getenv ("PKG_CONFIG_DEBUG_SPEW"))
    {
      want_debug_spew = TRUE;
      want_verbose_errors = TRUE;
      want_silence_errors = FALSE;
    }
  
  search_path = getenv ("PKG_CONFIG_PATH");
  if (search_path)
    {
      char **search_dirs;
      char **iter;

      search_dirs = g_strsplit (search_path, ":", -1);

      iter = search_dirs;
      while (*iter)
        {
          debug_spew ("Adding directory '%s' from PKG_CONFIG_PATH\n",
                      *iter);
          add_search_dir (*iter);

          ++iter;
        }

      g_strfreev (search_dirs);
    }

  pcbuilddir = getenv ("PKG_CONFIG_TOP_BUILD_DIR");
  if (pcbuilddir)
    {
      define_global_variable ("pc_top_builddir", pcbuilddir);
    }
  else
    {
      /* Default appropriate for automake */
      define_global_variable ("pc_top_builddir", "$(top_builddir)");
    }

  if (getenv ("PKG_CONFIG_DISABLE_UNINSTALLED"))
    {
      debug_spew ("disabling auto-preference for uninstalled packages\n");
      disable_uninstalled = TRUE;
    }
  
  opt_context = poptGetContext (NULL, argc, argv,
                                options_table, 0);

  result = poptGetNextOpt (opt_context);
  if (result != -1)
    {
      fprintf(stderr, "%s: %s\n",
	      poptBadOption(opt_context, POPT_BADOPTION_NOALIAS),
	      poptStrerror(result));
      return 1;
    }


  /* Error printing is determined as follows:
   *     - for --cflags, --libs, etc. it's on by default
   *       and --silence-errors can turn it off
   *     - for --exists, --max-version, etc. and no options
   *       at all, it's off by default and --print-errors
   *       will turn it on
   */

  if (want_my_version ||
      want_version ||
      want_libs ||
      want_cflags ||
      want_l_libs ||
      want_L_libs ||
      want_I_cflags ||
      want_list)
    {
      if (want_silence_errors && getenv ("PKG_CONFIG_DEBUG_SPEW") == NULL)
        want_verbose_errors = FALSE;
      else
        want_verbose_errors = TRUE;
    }
  else
    {
      /* Leave want_verbose_errors unchanged, reflecting --print-errors */
    }  
  
  if (want_my_version)
    {
      printf ("%s\n", VERSION);
      return 0;
    }

  if (required_pkgconfig_version)
    {
      if (compare_versions (VERSION, required_pkgconfig_version) >= 0)
        return 0;
      else
        return 1;
    }
  
  package_init ();

  if (want_list)
    {
      print_package_list ();
      return 0;
    }
  
  str = g_string_new ("");
  while (1)
    {
      pkgname = poptGetArg (opt_context);
      if (pkgname == NULL)
	break;

      g_string_append (str, pkgname);
      g_string_append (str, " ");
    }

  g_strstrip (str->str);  
  
  {
    GSList *reqs;
    GSList *iter;
    
    reqs = parse_module_list (NULL, str->str,
                              "(command line arguments)");      
    
    iter = reqs;
    
    while (iter != NULL)
      {
        Package *req;
        RequiredVersion *ver = iter->data;

        req = get_package (ver->name);

        if (req == NULL)
          {
            verbose_error ("No package '%s' found\n", ver->name);
            return 1;
          }

        if (!version_test (ver->comparison, req->version, ver->version))
          {
            verbose_error ("Requested '%s %s %s' but version of %s is %s\n",
                           ver->name,
                           comparison_to_str (ver->comparison),
                           ver->version,
                           req->name,
                           req->version);
              
            return 1;
          }

        packages = g_slist_prepend (packages, req);
        
        iter = g_slist_next (iter);
      }
  }

  g_string_free (str, TRUE);
  
  packages = g_slist_reverse (packages);

  if (packages == NULL)
    {      
      fprintf (stderr, "Must specify package names on the command line\n");
      
      exit (1);
    }
  
  if (want_exists)
    return 0; /* if we got here, all the packages existed. */

  if (want_uninstalled)
    {
      /* See if > 0 pkgs (including dependencies recursively) were uninstalled */
      GSList *tmp;
      tmp = packages;
      while (tmp != NULL)
        {
          Package *pkg = tmp->data;

          if (pkg_uninstalled (pkg))
            return 0;

          tmp = g_slist_next (tmp);
        }

      return 1;
    }
  
  if (want_version)
    {
      GSList *tmp;
      tmp = packages;
      while (tmp != NULL)
        {
          Package *pkg = tmp->data;

          printf ("%s\n", pkg->version);

          tmp = g_slist_next (tmp);
        }
    }

  if (required_exact_version)
    {
      Package *pkg = packages->data;
      
      if (compare_versions (pkg->version, required_exact_version) == 0)
        return 0;
      else
        return 1;
    }
  else if (required_atleast_version)
    {
      Package *pkg = packages->data;
      
      if (compare_versions (pkg->version, required_atleast_version) >= 0)
        return 0;
      else
        return 1;
    }
  else if (required_max_version)
    {
      Package *pkg = packages->data;
      
      if (compare_versions (pkg->version, required_max_version) <= 0)
        return 0;
      else
        return 1;
    }
  
  /* Print all flags; then print a newline at the end. */
  need_newline = FALSE;

  if (variable_name)
    {
      char *str = packages_get_var (packages, variable_name);
      printf ("%s", str);
      g_free (str);
      need_newline = TRUE;
    }
  
  if (want_I_cflags)
    {
      char *str = packages_get_I_cflags (packages);
      printf ("%s ", str);
      g_free (str);
      need_newline = TRUE;
    }
  else if (want_cflags)
    {
      char *str = packages_get_all_cflags (packages);
      printf ("%s ", str);
      g_free (str);
      need_newline = TRUE;
    }
  
  if (want_l_libs)
    {
      char *str = packages_get_l_libs (packages);
      printf ("%s ", str);
      g_free (str);
      need_newline = TRUE;
    }
  else if (want_L_libs)
    {
      char *str = packages_get_L_libs (packages);
      printf ("%s ", str);
      g_free (str);
      need_newline = TRUE;
    }
  else if (want_libs)
    {
      char *str = packages_get_all_libs (packages);
      printf ("%s ", str);
      g_free (str);
      need_newline = TRUE;
    }

  if (need_newline)
    printf ("\n");
  
  return 0;
}
