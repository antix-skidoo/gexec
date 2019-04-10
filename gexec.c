/***************************************************************************
 * gExec ~~ a GTK program launcher (aka "runner")
 *
 * Original Author  : Ferry Boender <electricmonk DOT nl>
 * License          : General Public License, Version 2, or later
 *
 * Copyright (C) 2004 Ferry Boender.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * 
 ****************************************************************************/

//
//      skidoo thanks  ZETCODE.COM  for keeping alive the GTK2 tutorials
//              http://zetcode.com/gui/gtk2
//
// It's 2019. The guh-nome camp sez gtk2 is "deprecated" and they have
// bastardized their pre-existing documentation by replacing each of the method descriptions
// with boilerplate "this blahblah is depecated and should not be used in new code"
// ( without mentioning what, if anything, each is replaced by within GTK3 )
//
// As of 2019, the glib GTK2 libraries remain part of the LSB (Linux Standard Base)
// As for GTK3, LSB still regards the GTK3 libs as trial/experimental.


#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>    // already in place, would have added it late, to support timeout sim keypress
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h>

#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>   // late, for iscntrl and isspace

#define OPT_TERM 1 // Run the command in a terminal
#define OPT_SU   2 // Run the command as root

#define DEFAULT_HISTMAX 40  // WAS  20 (The combo widget includes a scrollbar; why prevent user from setting a higher value?)

#define DEFAULT_CMD_SU "gksu '%s'"  // WAS  DOUBLEQUOTE INNER DELIMITERS    "gksu \"%s\""
//    The outer quotes here are consumed when gexec, first-run, generates the gexec.conf file
//    User can elect to change the inner quotes if desired, by editing  config file.



// ================   the following are defined in order of precedence (to be set as default, if found to be available)

#define DEFAULT_TERMURXVT "urxvt -e %s" 
//          must NOT place quotes around the cmd arg passed to urxvt. Provides a "-hold -e" option
//                      DOES NOT support Ctrl+Shift+C copy to clipboard.
//                      User can, must know to:
//                           drag-select a range text in the output
//                             { does user realize s/he can also  drag scrollup? }
//                           AND leave the urxvt window open,
//                           AND navigate to a paste destination
//                           THEN click middle mouse button.
//                           THEN (probably) navigate to, and close, the urxvt application window.
//                           BLECH!

#define DEFAULT_TERMROX "roxterm --disable-sm -e %s"  // {------ ok without, with single, or with double quotes. 
//                      Supports clipboard copy.
//                     No holdopen option. ( ANTIX CONFIGURED URXVT FOR use with ROX-FILER)

#define DEFAULT_TERMXFCE "xfce4-terminal -e \"%s\""
//          xfce4-terminal demands use of quotes (single or double) for the passed cmd arg
//                      Supports clipboard copy. Provides a "-H -e" holdopen option
//  IN TESTING, XFCE HAS "HUNG" ~~ 100% cpu utilization after launching via gexec, then closed (by redX? by File}}Exit?)
//  AS A TABBED TERMINAL, USER CAN "BREAK OUT OF JAIL" BY LAUNCHING GEXEC}}GKSU}}  THEN OPENING ADDITIONAL TABS?
//         ^------- CURRENTLY, (coincidentally) gksu denies launching it, treating the "with quotes" commandstring passed by gexec as being "dirty"
//  also:
//      Error retrieving accessibility bus address: org.freedesktop.DBus.Error.ServiceUnknown:
//      Failed to connect to the session manager: SESSION_MANAGER environment variable not defined

#define DEFAULT_TERMEM "xterm -e %s"
//      quotes around passed cmd arg are optional (ok with, or without single//double-quoted cmd arg)
//                      DOES NOT support Ctrl+Shift+C copy to clipboard. Provides a "-hold" option
//                      Not pre-installed in antiX.
//                      Display non-antialisaed xfonts.    BLECH!

#define DEFAULT_TERMALT "x-terminal-emulator -e %s"
//      IS NOT FOUND BY 'which' COMMAND.  INSTEAD, TO TEST, MUST  stat() or g_file_exists() or other
//      TODO:          for now, not gonna bother to test. Choose it as a last resort;
//                     Upon (edge case) failure, gexex prompts the user to inspect/edit gexec.conf
//                     WHATEVER IS SET AS THE x-t-e, IT MIGHT LACK A holdopen OPTION ANYHOW!

static GtkWidget *win_main;
static GtkWidget *pesky_chksu;
gboolean keepopen = FALSE;
gboolean want_debug = FALSE;
gchar *errordetails;
gchar *user_homedir, *ourconfdir, *ourconffile, *ourhistfile, *oldconffile, *oldhistfile = NULL;


struct _task {
	char *command;                // Current command
	int options;                  // Options (OPT_)
	char *cmd_termemu;            // Terminal emulator 
	char *cmd_su;                 // Root privilege provider 
	int history_max;              // Maximum number of history items to retain 
	GList *history;               // List of previous commands 
};


struct _settings {
	char *cmd_termemu; // Terminal emulator string to use 
	char *cmd_su;      // SuperUser program to use 
	int history_max;   // Maximum number of commands to retain in history list 
};


struct _task *task_new() {
	struct _task *ret_task = NULL;
	ret_task = malloc(sizeof(struct _task));
	ret_task->command = NULL;
	ret_task->options = 0;
	ret_task->cmd_termemu = NULL;
	ret_task->cmd_su = NULL;
	ret_task->history = NULL;
	return(ret_task);
}


GOptionEntry entries[] = 
{
  { "keepopen", 'k', 0, G_OPTION_ARG_NONE, &keepopen, "Keep gExec open after executing a command", NULL },
  { NULL }
};


void task_option_toggle(struct _task *task, int option) {
	task->options  ^= option;
}

void
task_command_set (struct _task *task, char *command) {
	assert(task != NULL);
	assert(command != NULL);

	if (task->command != NULL) {
		free(task->command);
	}

	task->command = strdup(command);
}

void
task_cmd_termemu_set (struct _task *task, char *cmd_termemu) {
	assert(task != NULL);
	assert(cmd_termemu != NULL);

	if (task->cmd_termemu != NULL) {
		free(task->cmd_termemu);
	}

	task->cmd_termemu = strdup(cmd_termemu);
}

void
task_cmd_su_set (struct _task *task, char *cmd_su) {
	assert(task != NULL);
	assert(cmd_su != NULL);

	if (task->cmd_su != NULL) {
		free(task->cmd_su);
	}

	task->cmd_su = strdup(cmd_su);
}





void
pathstrings_create ()
{
	user_homedir = g_strndup (  (gchar*)g_get_home_dir(),  1024  );   //     /home/demo    (no trailing slash)
	//   ^--- is subject to tainting, ala  getenv()
	ourconfdir   = g_build_filename (user_homedir, ".config", "gexec", NULL);
	ourconffile  = g_build_filename (user_homedir, ".config", "gexec", "gexec.conf", NULL);
	ourhistfile  = g_build_filename (user_homedir, ".config", "gexec", "gexec__history", NULL);  //     /home/demo/.config/gexec__history
	oldconffile  = g_build_filename (	user_homedir,		 ".gexec" , NULL);
	oldhistfile  = g_build_filename (	user_homedir,		 ".gexec_history" , NULL);
}

void
pathstrings_free () 
{
	g_free(user_homedir);
	g_free(ourconfdir);
	g_free(ourconffile);
	g_free(ourhistfile);
	g_free(oldconffile);
	g_free(oldhistfile);
}

void
settings_create () {
	FILE *f_out = NULL;

	if ( strlen(user_homedir) <= 2 ){                             //     ? guest login has no homedir?
		//       THE g_get_home_dir() DOC SEZ
		// "If the path given in HOME is non-absolute, does not exist, or is not a directory, the result is undefined."
		//       ^------------- DOES THE FN PRESUME $HOME var is set, is non-empty?
		// Well, strlen "returns the offset of the terminating null byte within the array"
		return;
	}

	f_out = fopen(ourconffile, "w");
	if (f_out == NULL)
	{         // attempt cleanup of prior version files
		if ( g_file_test (oldconffile, G_FILE_TEST_IS_REGULAR) )
			remove (oldconffile);
		
		if ( g_file_test (oldhistfile, G_FILE_TEST_IS_REGULAR) )
			remove (oldhistfile);
		
		if ( g_file_test (ourconfdir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) != TRUE )
			g_mkdir_with_parents (ourconfdir, 0700);
			
		FILE *fp;
		fp=fopen(ourconffile,"w");
		fclose(fp);
		fp=fopen(ourhistfile,"w");
		fclose(fp);
	}

	f_out = fopen (ourconffile, "w");
	if (f_out != NULL)
	{
		gchar *theurv = g_strconcat ("urxvt", NULL);
		gchar *foundurvxt = NULL;
		foundurvxt = g_strconcat(g_find_program_in_path (theurv), NULL);

		gchar *therox = g_strconcat ("roxterm", NULL);
		gchar *foundroxterm = NULL;
		foundroxterm = g_strconcat (g_find_program_in_path (therox), NULL);

		gchar *thexfce = g_strconcat ("xfce4-terminal", NULL);
		gchar *foundxfce = NULL;
		foundxfce = g_strconcat (g_find_program_in_path (thexfce), NULL);

		gchar *thexterm = g_strconcat ("xterm", NULL);
		gchar *foundxterm = NULL;
		foundxterm = g_strconcat (g_find_program_in_path (thexterm), NULL);

		if (foundurvxt != NULL){
			fprintf(f_out, "cmd_termemu=%s\n", DEFAULT_TERMURXVT);
		}
		else if (foundroxterm != NULL){
			fprintf(f_out, "cmd_termemu=%s\n", DEFAULT_TERMROX);
		}
		else if (foundxfce != NULL){
			fprintf(f_out, "cmd_termemu=%s\n", DEFAULT_TERMXFCE);
		}
		else if (foundxterm != NULL){
			fprintf(f_out, "cmd_termemu=%s\n", DEFAULT_TERMEM);
		} else {
			fprintf(f_out, "cmd_termemu=%s\n", DEFAULT_TERMALT);
		}
		g_free (therox);
		g_free (foundroxterm);
		g_free (theurv);
		g_free (foundurvxt);
		g_free (thexfce);
		g_free (foundxfce);
		g_free (thexterm);
		g_free (foundxterm);
 
		fprintf(f_out, "cmd_su=%s\n", DEFAULT_CMD_SU);
		fprintf(f_out, "history_max=%i\n", DEFAULT_HISTMAX);
		fclose(f_out);  /////////////  hmmmm, if f_out HAD been null, still need to close the handle?
	}
	return;
}


struct _settings *settings_read() {
	FILE *f_in = NULL;
	struct _settings *settings = NULL;
	char line[1024];    // ========================   howdy         1024 should be sufficient ?
	settings = malloc(sizeof(struct _settings));

	gchar *theurv = g_strconcat("urxvt", NULL);
	gchar *foundurvxt = NULL;
	foundurvxt = g_strconcat (g_find_program_in_path (theurv), NULL);

	gchar *therox = g_strconcat("roxterm", NULL);
	gchar *foundroxterm = NULL;
	foundroxterm = g_strconcat (g_find_program_in_path (therox), NULL);

	gchar *thexfce = g_strconcat("xfce4-terminal", NULL);
	gchar *foundxfce = NULL;
	foundxfce = g_strconcat (g_find_program_in_path (thexfce), NULL);

	gchar *thexterm = g_strconcat("xterm", NULL);
	gchar *foundxterm = NULL;
	foundxterm = g_strconcat (g_find_program_in_path (thexterm), NULL);

	if (foundurvxt != NULL){
		settings->cmd_termemu = DEFAULT_TERMURXVT;
	}
	else if (foundroxterm != NULL){
		settings->cmd_termemu = DEFAULT_TERMROX;
	}
	else if (foundxfce != NULL){
		settings->cmd_termemu = DEFAULT_TERMXFCE;
	}
	else if (foundxterm != NULL){
		settings->cmd_termemu = DEFAULT_TERMEM;
	} else {
		settings->cmd_termemu = DEFAULT_TERMALT;
	}
	g_free (therox);
	g_free (foundroxterm);
	g_free (thexfce);
	g_free (foundxfce);
	g_free (theurv);
	g_free (foundurvxt);
	g_free (thexterm);
	g_free (foundxterm);

	settings->cmd_su = DEFAULT_CMD_SU;
	settings->history_max = DEFAULT_HISTMAX;

	f_in = fopen(ourconffile, "r");
	if (f_in != NULL) { 
		while (fgets(line, 1024, f_in)) {
			char *newline_pos;
			char **tokens;
			newline_pos = strchr(line, '\n');
			newline_pos[0] = '\0';
			tokens = g_strsplit(line, "=", 2);

			if (strcmp(tokens[0], "cmd_termemu") == 0 && tokens[1] != NULL) {
				settings->cmd_termemu = strdup(tokens[1]);
			}
			if (strcmp(tokens[0], "cmd_su") == 0 && tokens[1] != NULL) {
				settings->cmd_su = strdup(tokens[1]);
			}
			if (strcmp(tokens[0], "history_max") == 0 && tokens[1] != NULL) {
				settings->history_max = atoi(tokens[1]);  // returns 0 if non-numeric
			}
			g_strfreev(tokens);  // tokens is an array of strings
		}
		fclose(f_in);
	} else {
		settings_create();  // No settings yet; create default settings
	}

	return(settings);
}


GList *history_read()
{
	FILE *f_in = NULL;
	GList *history = NULL;
	char line[1024];

	f_in = fopen(ourhistfile, "r");
	if (f_in != NULL) {
		while (fgets(line, 1024, f_in)) {
			char *newline_pos;
			newline_pos = strchr(line, '\n');
			newline_pos[0] = '\0';
			if (strlen(line) >= 1)  ///// manual edits may have introduced blank lines
				history = g_list_append(history, strdup(line));
		}
		fclose(f_in);
	}
	return(history);
}


// sole caller is ui_cmd_run() and, currently, it disregards the returned value
int
history_write(GList *history, int history_max) {
	
	if (history_max == 0)
		return 0;

	FILE *f_out;
	GList *cur = NULL;
	int count = 0;
	int erwrite = 0;

	cur = history;
	f_out = fopen(ourhistfile, "w");

	if (f_out != NULL) {
		while (cur != NULL && count != history_max) {
			fprintf(f_out, "%s\n", (char *)cur->data);
			cur = g_list_next(cur);
			count++;
		}

		fclose(f_out);
	} else {
		erwrite = -1;
	}
	return(erwrite);
}


void
task_history_add(struct _task *task, char *command) {
	GList *new_history = NULL;
	GList *iter = NULL;
	assert(task != NULL);
	assert(command != NULL);

	new_history = g_list_append(new_history, strdup(command));
	iter = task->history;
	while (iter != NULL) {
		// Skip duplicates of 'command'
		if (strcmp(iter->data, command) != 0) {
			new_history = g_list_append(new_history, strdup(iter->data));
		}
		free(iter->data);
		iter = g_list_next(iter);
	}
	g_list_free(task->history);
	task->history = new_history;
}




gboolean
starts_with(const char *a, const char *b){
   if(strncmp(a, b, strlen(b)) == 0)
     return TRUE;
   return FALSE;
}

/**
void
example_iterateover (void)
{
  char *colors[] = {"peach", "green", "tan", ""}, **n;
  n = colors;
  while (*n != "") {
    printf ("%s\n",  *n++);
  }
  return;
}
*/

int
finalcommand_run(struct _task *task, char **errormsg) {  //////////   SOLE CALLER IS  ui_cmd_run()
	char *final_command = NULL;
	int argc;
	char **argv;
	GError *shellparse_err = NULL;
	int ernum = 0;
	
	assert(task->command != NULL);
	assert(task->cmd_termemu != NULL);

	// Handle execution options. If any options are set, wrap the current command in the appropriate command.
	final_command = strdup(task->command);
	if ( strlen(final_command) < 1)
	{
		free(final_command);
		return(-1);
	}

/**              //    "w" is a command  (displays who is logged in)
	const
	char *sw_matches[] =
      {
       "su ", "/bin/su ", "sudo ", "/bin/sudo ", "sh ", "/bin/sh ", "ash ", "/bin/ash ", "bash ", "/bin/bash ",
       "dash ", "/bin/dash ", "zsh ", "/bin/zsh ", "rzsh ", "bin/rzsh ", "zsh5 ", "/bin/zsh5",
       ""
      }, **n;
	
	const
	char zact_matches =
      {
       "sh", "ash", "bash", "dash", "zsh", "rzsh", "zsh5",
       ""
      }, **n;
}

	//  Begs use of a curated list rather than an algorithm.
	//  Even if we check exists, is executable by the user, contains a hashbang...
	//  algorithm would need to determine whether progX accepts (or not)(or demands) commandline options.
	//  Also, "progX" could be a symlink, an alias...
	//  or it might reside in a sudo//root -protected path (and/or covered by a rule within sudoers file(s)).
	// 
	//  Helptext already disclaims: "it is your responsibility to know/choose when RunInTerminal is appropriate"
	
	//  Not helpful (or not bulletproof) to sniff !# or .sh file extention
*/

/*
		//  far too many to cover. Even if we consider the likely (pasted command) common cases
		//    are we gonna test options?    sudo ... -H
	if (starts_with(final_command, "su ") == TRUE  ||
	    starts_with(final_command, "/bin/su ") == TRUE  ||
			// POINTLESS    strcmp(final_command, "su ") == 0  ||
        starts_with(final_command, "sudo ") == TRUE ||
        starts_with(final_command, "/usr/bin/sudo ") == TRUE  ||

        starts_with(final_command, "sh ") == TRUE   ||
        starts_with(final_command, "/bin/sh ") == TRUE  ||
			strcmp(final_command, "sh") == 0  ||
        starts_with(final_command, "ash ") == TRUE   ||
        starts_with(final_command, "/bin/ash ") == TRUE  ||
			strcmp(final_command, "ash") == 0  ||
        starts_with(final_command, "bash ") == TRUE  ||
        starts_with(final_command, "/bin/bash ") == TRUE  ||
			strcmp(final_command, "bash") == 0  ||
        starts_with(final_command, "dash ") == TRUE  ||
        starts_with(final_command, "/bin/dash ") == TRUE  ||
			strcmp(final_command, "dash") == 0  ||
        starts_with(final_command, "zsh ") == TRUE   ||
        starts_with(final_command, "/bin/zsh ") == TRUE   || 
			strcmp(final_command, "zsh") == 0   ||
	// FUTILE. Consider the case
	//       zsh myscript   {----- if an error, is it due to zsh not installed, myscript not in path, typo...?
	//       bash myscript  {----- if no error, BLIP and it's gone, with ensuring user managed view any output
*/
    if (  (task->options & OPT_TERM) == OPT_TERM      )
	{      ////////  nixme?      We now test emu availability in settings_check()
		int buf_size = (strlen(final_command) + strlen(task->cmd_termemu));
		char *tmp_command  = malloc( sizeof(char) * (buf_size + 1) );
		if (snprintf(tmp_command, buf_size, task->cmd_termemu, final_command) >= buf_size) {
			ernum = -2; //  indicates Invalid term_emu string (read from user-edited conf file)
		} else {
			shellparse_err = NULL;
			if (g_shell_parse_argv(tmp_command, &argc, &argv, &shellparse_err) == FALSE) {
				ernum = -1; // indicates Invalid command syntax, as reported by g_shell_parse_argv()
				if (shellparse_err != NULL)
				{
					// WE SHOULD BE ABLE TO RETREIVE A VERBOSE GError MESSAGE, e.g.
					//        Text ended before matching quote was found for '. (The text was 'urxvt -e '')
					errordetails = strdup(shellparse_err->message);
					printf ("%s\n", shellparse_err->message);
					g_error_free(shellparse_err);
				}
			}
			final_command = strdup(tmp_command);
		}
		free(tmp_command);
	}
	
	if ( ernum == 0  &&  (task->options & OPT_SU) == OPT_SU)
	{
		int buf_size = (strlen(final_command) + strlen(task->cmd_su));
		char *tmp_command = malloc(sizeof(char) * (buf_size + 1) );
		gchar *foundgksu = NULL;
		foundgksu = g_strconcat (g_find_program_in_path ("gksu"), NULL);
		if ( (snprintf(tmp_command, buf_size, task->cmd_su, final_command) >= buf_size)  ||
		     (foundgksu == NULL)  )
		{
			ernum = -3; // gksu command is unavailable (should never happen)
		} else {
			shellparse_err = NULL;
			if (g_shell_parse_argv(tmp_command, &argc, &argv, &shellparse_err) == FALSE) {
				ernum = -1; // indicates Invalid command syntax, as reported by g_shell_parse_argv()
				if (shellparse_err != NULL)
				{
					errordetails = strdup(shellparse_err->message);
					printf ("%s\n", shellparse_err->message);
					g_error_free(shellparse_err);
				}
			}
			final_command = strdup(tmp_command);
		}
		g_free (foundgksu);
		free(tmp_command);
	}

	//////////     SANITIZATION MUST OCCUR PRIOR TO THIS POINT   //////////////
	//if (want_debug == TRUE)
	//	printf ("----- final_command:\n%s\n", final_command);


	// g_shell_parse_argv() 
	// Parses a command line into an argument vector, in much the same way the shell would,
	// but without many of the expansions the shell would perform
	// (variable expansion, globs, operators, filename expansion, etc. are not supported).
	// The results are defined to be the same as those you would get from a UNIX98 /bin/sh,
	// as long as the input contains none of the unsupported shell expansions.
	// If the input does contain such expansions, they are passed through literally.
	//
	//  tested:    tilde expansion is not performed
	//             start-of-line #, or "abcde #fgh" space+poundsign, is treated as comment
	//             questionmark (globbing char) is treated as a literal
	//             leading "dot space" (instructing shell to source somefile) is disallowed
	//             "dot space'
	//             leafpad ../neo.txt     (successfully loads, for example, /home/neo.txt)
	//             leafpad && somecommand           opens newdoc "&&" into leafpad
	//             leafpad; galculator       failed. no command named leafpad;
	//             leafpad& galculator (also    leafpad&&    leafpad ||    leafpad ;  ) 
	//                                       not received by shell as multiple commands
	shellparse_err = NULL;
	if (ernum == 0  &&  g_shell_parse_argv(final_command, &argc, &argv, &shellparse_err) == FALSE)
	{
		ernum = -1; // indicates Invalid command syntax (as reported by g_shell_parse_argv() )    peep
		if (shellparse_err != NULL)
		{
			errordetails = strdup(shellparse_err->message);
			printf ("%s\n", shellparse_err->message);
			g_error_free(shellparse_err);
		}
	}
	
	if (ernum == 0)
	{
		GPid child_pid; // no pipes, and we never utilize this var. Could nix (and pass NULL in 7th arg to g_spawn_async)
		GError *error = NULL;
		argv[argc] = NULL;
                       // working dir, argv, envp, spawn flags, child_setup (pre-execfn), gpointer, childPID, **error
		if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &child_pid, &error))
		{
			ernum = -4;  //  indicates failure to execute the specified command (per glib GSpawnError errormsg)
			*errormsg = strdup(error->message);

			// =============== here  (inside IF NO ERROR CONDITION) IS WHERE WE ARGUABLY SHOULD CREATE A COMMANDHISTORY ENTRY.
			//   howdy    todo:  reconsider which, in the absence of full logging, is preferable
			//               BY NOT DOING SO, "FAILED" COMMANDSTRINGS WIND UP LISTED IN THE HISTORY. ON THE OTHER HAND,
			//               IF WE EXCLUDED FAILED COMMANDS, USER CANNOT REFER BACK (TO LEARN FROM PRIOR MISTYPES/MISTAKES)
			//   currently, I'm inclined to add unconditional logging and cease adding failed commands into history picklist
			
			g_error_free(error);
		}
	}

	free(final_command);
	return(ernum);
}


void
ui_errdialog(char *message)
{
	GtkWidget *dialog;   // CANNOT EASILY ACCESS its secondary markup text child to set/request a wider width!
	dialog = gtk_message_dialog_new_with_markup((GtkWindow *)win_main, GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "<b>Error:</b>\n\n %s", message);
	gtk_widget_set_size_request (dialog, 580, -1);
	//gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW(dialog), "_");  // avoid "Unnamed"
	
	//gtk_window_reshow_with_initial_size ( (GtkWindow *)dialog );
	//gtk_window_present( (GtkWindow *)dialog );
	////////// WAS attempting to raise its stacking order, to avoid being obscured by dropdown

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}


// called from  ui_btn_ok_cb_clicked   AND   from ui_combo_command_cb_activate
void
ui_cmd_run(struct _task *task) {
	int err = 0;
	char *errormsg = NULL;

	if (strlen(task->command) < 1) {
		ui_errdialog("nothing to do ~~ the entrybox is currently empty");
		return;
	}

	if (strlen(task->command) > 640)
	{
		ui_errdialog("EXCESSIVELY LONG COMMANDSTRING\n\nBecause overly long strings typically indicate content has "  \
		      "been blindly PASTED, and such content cannot be easily inspected (by left--right scrolling to view "  \
		      "within the tiny entrybox), gexec will refuse to process commandstrings longer than 640 characters.");
		return;  // by design, to annoy, we do not blank the commandstring (nor restore entrybox focus + cursor position)
	}

	err = finalcommand_run(task, &errormsg);  // xref: "ernum"
	switch (err) {
		case -1:
			ui_errdialog("Invalid command syntax\n\n (or empty, no command specified)\n\n\nTip: " \
			    "a 'syntax error' often indicates the presence of unclosed (unbalanced, unpaired) quotation marks.");
			break;
		case -2:
			ui_errdialog("invalid Terminal Emulator launchstring\n\nInspect (edit) your ~/.config/gexec/gexec.conf file" \
			             "\n\nTip: the gexec manpage provides some examples");
			break;
		case -3:   // stated as a pkg dependency; handle case of possibile manual deletion
			ui_errdialog("gksu (permissions-granting command) not found\n\nTo use this gexec feature, gksu must be installed.");
			break;
		case -4:    //  Failure to execute command (per glib GSpawnError errormsg)
					//         ?not found in path  ?not installed  ?only available to sudo|wheel|root
					//    COULD CAPTURE AND CHANGE ITS TEXT (vs TOO OFTEN SEEN, AND MISLEADING, "blahblah  File not found.")
					//    BUT I DON'T KNOW WHETHER IT IS SHELL-SPECIFIC AND/OR IS LOCALIZED
			errormsg = g_strconcat(errormsg, "\n\n\nThe above generic errormsg is handed to gexec by the operating system. " \
			    " 'File not found' may indicate 'command not found in path' (maybe requires sudo|root permission) or it" \
			    " might simply indicate that you have mistakenly typed the command name or the /path/to/commandname", NULL);
			ui_errdialog(errormsg);
			free(errormsg);
			break;
			
		case -5:   // terminal emulator program specified in gexec.conf has been uninstalled or is otherwise unavailable
			ui_errdialog("cannot find the terminal Emulator program spcified in you\n\n~/.config/gexec/gexec.conf file");
			break;         //     nixme   settings_check() would have assigned x-terminal-emulator
			                   // (which may be misconfigured, but would not cause us to arrive here due to "cannot find")
		case -6:
			////// task->command has been emptied|blanked just now      ========= howdy  currently this case is unused
			break;
		default:
			task_history_add(task, task->command);
			history_write(task->history, task->history_max);
			if (keepopen == TRUE)
			{      // user must re-select "RunAsRoot" for each issued command
				gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(pesky_chksu), FALSE );
				if (  (task->options & OPT_SU) == OPT_SU  )   // bitwise joy
				{
					task_option_toggle(task, OPT_SU);
				}
			}

			//  not treating this as an else condition, pending addition of TBD
			//     (log handling) features which may not be mutially exclusive
			if (keepopen != TRUE)
			{
				gtk_main_quit();
			}
			break;
	}
}


// Main window callbacks 
void
ui_win_main_cb_delete_event (GtkWidget *widget, void *data) {
	gtk_main_quit();
}

// called solely by combo->entry "activate" signal
void
ui_combo_command_cb_activate (GtkWidget *widget, struct _task *task)
{
	task_command_set(task, (char *)gtk_entry_get_text(GTK_ENTRY(widget)));
	ui_cmd_run (task);
}



// called solely as event handler for combo->entry "key-press-event" signal
gboolean
ui_combo_command_cb_key_press_event(GtkWidget *widget, GdkEventKey *event, struct _task *task)
{
	if (event->keyval == GDK_Escape) {
		gtk_main_quit();
	} else {
		task_command_set( task, (char *)gtk_entry_get_text(GTK_ENTRY(widget)) );
	}
	return(FALSE);
}





static void
gk_info_dialog (GtkMessageType type, gchar *format, ...)
{
  GtkWidget *diag_win;
  va_list ap;
  gchar *msg;

  va_start(ap, format);
  msg = g_strdup_vprintf(format, ap);
  va_end(ap);

  diag_win = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE, NULL);
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG(diag_win), msg);
  gtk_window_set_title (GTK_WINDOW(diag_win), "_");  // avoid "Unnamed"
  gtk_widget_set_size_request (diag_win, 520, -1); 

  gtk_window_set_position (GTK_WINDOW(diag_win), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW(diag_win), FALSE);
  gtk_dialog_set_default_response(GTK_DIALOG(diag_win), -3); // intent is to force OK button click (prevent Enter key from activating response)

  gtk_widget_show_all (diag_win);
  gtk_dialog_run (GTK_DIALOG(diag_win));

  g_free (msg);
  gtk_widget_destroy (diag_win);
}


gchar *controlwarnmessage = "the commandstring (pasted?) contained control character(s) and has been rejected\n";

//   Yes, toward accommodating URI fragments, we will allow ampersand, questionmark, poundsign, and percent.
//   Yes, PERCENT char is arguably "legal" (may indicate URLencoded string (%20) or filename containing spaces).
//          http://www.tldp.org/LDP/abs/html/parameter-substitution.html#PSUB2
//   In the absence of curly braces, poundsign and percent should be regarded as harmless

gchar *illegalcharmessage = "gexec will refuse to process a commandstring containing any of the following characters:\n\n" \
		     "   $  DOLLARSIGN\n   \\   BACKSLASH\n   `  BACKTICK\n   &gt;  GREATER_THAN\n   &lt;  LESS_THAN\n   ^  CARET\n" \
		     "   {  LEFT_CURLY_BRACE\n   (  LEFT_PARENTHESIS\n   [  LEFT_SQUARE_BRACKET\n   *  ASTERISK\n   !  EXCLAMATION_POINT\n\n" \
		     "   ref:  <i>www.tldp.org/LDP/abs/html/special-chars.html</i>\n\n\n" \
		     "Tip: these \"special characters\" are the building blocks of shell scripts. If your commandstring contains" \
		     " any of these, you can place it inside a script and launch the script, by name, via gexec.\n\n"; // escape gt+lt for pango

//          =========== THIS SERVES AS SINGLE ENTRYPOINT
gboolean
ui_combo_command_cb_changed(GtkWidget *widget, struct _task *task) {
	//printf ("%s\n", "-----changed----");
	gchar *mygcs;
	mygcs = g_strdup((gchar *)gtk_entry_get_text(GTK_ENTRY(widget)));
	g_strdelimit (mygcs, "\n", '\0');  // truncate       xref:  g_strcanon()
	g_strchug (mygcs);     //  g_strstrip would remove leading+trailing whitespace
	g_strdelimit (mygcs, "\t", ' ');  // convert tab chars to space

	if (strlen(mygcs) >= 1)
	{
		int i;
		int j = strlen(mygcs);
		for (i=0; i < j; i++  )
		{
			// we are monitoring CHANGED signal, not keypress (where a \b Backspace should be exempted)
			if (iscntrl(mygcs[i]) != 0)
			{
				task_command_set(task, "");
				gtk_entry_set_text(GTK_ENTRY(widget), "");
				gk_info_dialog (GTK_MESSAGE_ERROR, controlwarnmessage);
				return(TRUE);
			}
					//  note:  strict here, and we do NOT shell_escape the final result
			if (  (strchr(&mygcs[i],'$')!= NULL)  ||
				(strchr(&mygcs[i],'`')  != NULL)  ||
				(strchr(&mygcs[i],'\\') != NULL)  ||
				(strchr(&mygcs[i],'<')  != NULL)  ||
				(strchr(&mygcs[i],'>')  != NULL)  ||
				(strchr(&mygcs[i],'^')  != NULL)  ||
				(strchr(&mygcs[i],'{')  != NULL)  ||
				(strchr(&mygcs[i],'(')  != NULL)  ||
				(strchr(&mygcs[i],'[')  != NULL)  ||
				(strchr(&mygcs[i],'*')  != NULL)  ||
				(strchr(&mygcs[i],'!')  != NULL)    )
			{
				gtk_entry_set_text(GTK_ENTRY(widget), "");
				task_command_set(task, "");
				gk_info_dialog (GTK_MESSAGE_ERROR, illegalcharmessage);
				return(TRUE);
			}
		}
	}

	task_command_set(task, (char *)mygcs);
	gtk_entry_set_text(GTK_ENTRY(widget), mygcs );
	g_free (mygcs);
	return(FALSE);
}



void
ui_chk_term_cb_toggled(GtkWidget *widget, struct _task *task) {
	task_option_toggle(task, OPT_TERM);
}


void
ui_chk_su_cb_toggled (GtkWidget *widget, struct _task *task) {
	task_option_toggle (task, OPT_SU);
}

void
ui_chk_keepopen_cb_toggled (GtkWidget *widget, struct _task *task) {
	keepopen = !keepopen;
}

void
ui_btn_ok_cb_clicked (GtkWidget *widget, struct _task *task) {
	ui_cmd_run (task);
}

void
ui_btn_cancel_cb_clicked (GtkWidget *widget, void *data) {
	gtk_main_quit();
}


gchar *lottahelptext =  "When items are present in the gexec history list, pressing downArrow key recalls "  \
      "the most recent history item; upArrow recalls earliest item in the list.\n\n"  \

      "NOTE: gexec does not know whether a given program must be launched within "  \
      "a terminal emulator in order to view the program output, nor whether a given "  \
      "program expects you to interactively supply further input by typing. It is up "  \
      "to you to decide when the 'Run In Terminal' option is appropriate.\n\n"  \

      "When 'Keep Open' is in effect, by design (to guard against accidents) "  \
      "after launching a command, gexec automatically UNticks the 'Run as root' option "  \
      "if it had been ticked. (This option must be re-selected each time it is used.)\n\n"  \

      "You can edit ~/.config/gexec/gexec.conf to specify a different number of "  \
      "stored history list entries (or specify 0 to disable history logging). You can "  \
      "also specify an alternative terminal emulator program for use with the gexec "  \
      " 'Run in terminal' option.\n\n"  \

      "For additional usage info, refer to /usr/share/doc/gexec/*\nand the gexec manpage";


static void
ui_helpdialog (gpointer window, GtkMessageType type, gchar *format, ...)
{
  GtkWidget *help_win;
  va_list ap;
  gchar *msg;

  va_start(ap, format);
  msg = g_strdup_vprintf(format, ap);
  va_end(ap);

  help_win = gtk_message_dialog_new ( GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE, NULL);
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG(help_win), msg);
  gtk_window_set_title (GTK_WINDOW(help_win), "gexec Help / Tips"); 
  gtk_widget_set_size_request (help_win, 560, -1); 
  gtk_window_set_resizable (GTK_WINDOW(help_win), TRUE);
  gtk_window_set_modal(GTK_WINDOW(GTK_DIALOG(help_win)), TRUE);   // currently, its behavior _is_ TRULY modal
  gtk_window_set_position (GTK_WINDOW(help_win), GTK_WIN_POS_CENTER);
  //gtk_dialog_set_default_response(GTK_DIALOG(help_win), -3);
  // intent here is (was) to force OK button click (prevent Enter key from activating response)

  gtk_widget_show_all (help_win);
  gtk_dialog_run (GTK_DIALOG(help_win));

  g_free (msg);
  gtk_widget_destroy (help_win);
}

void
ui_btn_help_cb_clicked (GtkWidget *widget, gpointer window) {
	ui_helpdialog (window, GTK_MESSAGE_INFO, lottahelptext);  
}



gchar *pastewarnmessage = "REMINDER:\ncopypasting commandstrings\nfrom web pages or from " \
         "other markup//rich-text formatted documents is ill-advised !\n\n\n" \
         "THIS REMINDER IS SO IMPORTANT THAT IT BEARS REPEATING\n " \
         "( EVERY TIME YOU PERFORM A PASTE ACTION HERE )\n";


//  CURRENTLY, only called as "paste-clipboard" handler
gboolean 
entrypastemenu_cb (GtkWidget *w, GdkEvent *event, char data)  //   data passed in is task->command
{
  gk_info_dialog (GTK_MESSAGE_INFO, pastewarnmessage);
  //  ok to passthru to other handlers (NOTED: following a paste, cursor is at start-of-text)
  return FALSE;
}


gboolean
entryclicked_cb (GtkWidget *w, GdkEventButton *event, char data) {
	if (event->type == GDK_BUTTON_PRESS &&  event->button == 2)
	{
		gk_info_dialog (GTK_MESSAGE_INFO, pastewarnmessage);
		return TRUE; // late discovery. need TRUE here
	}

	// must use false (not fully handled, allow other handlers) else mouse3 ContextMenu is suppressed
	return FALSE;
}


//   not called during Mouse2 selection paste
//
// data is win_main, but we don't make use of it (could, passing to gk_info_dialog)
gboolean
entrypastekey_cb (GtkWidget *w, GdkEventKey *event, gpointer data)
{
  gboolean wantpaste = FALSE;
  switch (event->keyval)
  {
    case GDK_v:
    case GDK_V:
      if (event->state & GDK_CONTROL_MASK)
        wantpaste = TRUE;
      break;
    case GDK_KEY_Insert:
    case GDK_KEY_KP_Insert:
      if (event->state & GDK_SHIFT_MASK)
        wantpaste = TRUE;
      break;
    default:
      return FALSE;
  }

  if (wantpaste)
  {
    gk_info_dialog (GTK_MESSAGE_INFO, pastewarnmessage);  //  verbiage: "rich text"?
    //return TRUE; // TRUE to indicate that the event has been handled, it should not propagate further. 
    ///////// 
    /////////  THE DIALOG (ANNOYANCE) SHOULD SUFFICE, RATHER THAN BLOCKING THE PASTE 
    /////////                                    (WHICH WOULD REPRESENT AN ANTI-FEATURE) 
  }
  return FALSE;
}


//    THIS SUPPRESSES AUTOMATICALLY PREPOPULATING THE LAST-RUN COMMAND INTO ENTRYBOX
static gboolean
on_loaded_timeout (gpointer user_data)
{
  GtkWidget *widget = user_data;
  GdkKeymapKey *keys;
  gint n_keys;

  if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_default (), GDK_o, &keys, &n_keys))
  {
    guint16 hardware_keycode;
    GdkEvent *event;
    hardware_keycode = keys[22].keycode;
    g_free (keys);
    
    event = gdk_event_new (GDK_KEY_PRESS);
    event->key.window = g_object_ref (widget->window);
    event->key.state = 0;
    event->key.hardware_keycode = hardware_keycode;
    event->key.keyval = gdk_unicode_to_keyval (32);
    event->key.length = 1;
    event->key.string = g_strdup ("Backspace");
    event->key.send_event = FALSE;
    event->key.time = GDK_CURRENT_TIME;   
    gtk_main_do_event (event);
    gdk_event_free (event);
  }
  return FALSE;
}

static gboolean
ski_key_press_details (GtkWidget *widget, GdkEventKey *event)
{
  if (want_debug == TRUE) {
      g_print ("send_event=%d, state=%u, keyval=%u, length=%d, string='%s', hardware_keycode=%u, group=%u\n",
           event->send_event, event->state,
           event->keyval, event->length, event->string, event->hardware_keycode, event->group);
  }
  return FALSE;
}







int
main (int argc, char *argv[]) {
	GOptionContext *context = NULL;
	GError *error = NULL;
	struct _task *task = NULL;
	struct _settings *settings = NULL;
	GtkWidget *chk_term, *chk_su, *chk_keepopen;
	GtkWidget *lbl_command;
	GtkWidget *combo_command;
	GtkWidget *hbox_command, *vbox_cmdstack, *hbox_options, *vbox_optionstack, *hbox_buttons;
	GtkWidget *btn_ok, *btn_cancel, *btn_help, *vbox_main;
	
	pathstrings_create();
	
	// Handle commandline options
	context = g_option_context_new ("- Interactive run dialog"); // stdout helpmsg
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		fprintf (stderr, "%s\n", error->message);
		g_error_free (error);
		exit(1);
	}
  
	task = task_new();
	task->history = history_read ();
	
	settings = settings_read ();
	task->cmd_termemu = settings->cmd_termemu;
	task->cmd_su = settings->cmd_su;
	task->history_max = settings->history_max;

	gtk_init (&argc, &argv);

	win_main = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW(win_main), "gExec -- launch a program, run a command");
	gtk_window_set_default_size (GTK_WINDOW(win_main), 480, -1);
	gtk_signal_connect (GTK_OBJECT(win_main),
		"delete_event", GTK_SIGNAL_FUNC(ui_win_main_cb_delete_event), NULL);

	hbox_command = gtk_hbox_new(FALSE, 0);
	vbox_cmdstack = gtk_vbox_new(FALSE, 0);

	lbl_command = gtk_label_new ("<b> Run command</b>:");
	gtk_misc_set_alignment (GTK_MISC(lbl_command), 0.0, 0);
	gtk_label_set_use_markup (GTK_LABEL(lbl_command), TRUE);

	combo_command = gtk_combo_new ();
	gtk_combo_set_use_arrows_always (GTK_COMBO(combo_command), TRUE);
	gtk_combo_disable_activate (GTK_COMBO(combo_command));
	//////////// NECESSARY to avoid dropdown displaying at inopportune times

    gtk_signal_connect (GTK_OBJECT(combo_command),
		"key-release-event", GTK_SIGNAL_FUNC(entrypastekey_cb), win_main);
	// use RELEASE here, to avoid dialog twice at ctrl+v and shift_ins

	gtk_signal_connect (GTK_OBJECT(GTK_COMBO(combo_command)->entry),
		"activate", GTK_SIGNAL_FUNC(ui_combo_command_cb_activate), task);

	gtk_signal_connect (GTK_OBJECT(GTK_COMBO(combo_command)->entry),
		"key-press-event", GTK_SIGNAL_FUNC(ui_combo_command_cb_key_press_event), task);
	gtk_signal_connect (GTK_OBJECT(GTK_COMBO(combo_command)->entry),
		"changed", GTK_SIGNAL_FUNC(ui_combo_command_cb_changed), task);
	gtk_signal_connect (GTK_OBJECT(GTK_COMBO(combo_command)->entry),
		"paste-clipboard", GTK_SIGNAL_FUNC(entrypastemenu_cb), task->command);

	gtk_signal_connect (GTK_OBJECT(GTK_COMBO(combo_command)->entry),
		"button-press-event", GTK_SIGNAL_FUNC(entryclicked_cb), task->command);
	// catch mouse2 selection paste

	if (task->history != NULL) {
		gtk_combo_set_popdown_strings (GTK_COMBO(combo_command), task->history);
	}

	gtk_box_pack_start (GTK_BOX(vbox_cmdstack), lbl_command, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_cmdstack), combo_command, TRUE, TRUE, 0);

	hbox_options = gtk_hbox_new(FALSE, 0);
	vbox_optionstack = gtk_vbox_new(FALSE, 0);

	chk_term = gtk_check_button_new_with_mnemonic("Run in _terminal");
	gtk_signal_connect (GTK_OBJECT(chk_term),
		"toggled", GTK_SIGNAL_FUNC(ui_chk_term_cb_toggled), task);
	gtk_box_pack_start (GTK_BOX(vbox_optionstack), chk_term, FALSE, FALSE, 0);

	// Display "Run as root" checkbox only if not already running as root
	if (geteuid() != 0) {
		chk_su = gtk_check_button_new_with_mnemonic ("Run as _root");
		pesky_chksu = chk_su;
		gtk_signal_connect (GTK_OBJECT(chk_su),
			"toggled", GTK_SIGNAL_FUNC(ui_chk_su_cb_toggled), task);
		gtk_box_pack_start (GTK_BOX(vbox_optionstack), chk_su, FALSE, FALSE, 0);
	}

	chk_keepopen = gtk_check_button_new_with_mnemonic ("Keep Open this dialog after launch");
	gtk_signal_connect (GTK_OBJECT(chk_keepopen),
		"toggled", GTK_SIGNAL_FUNC(ui_chk_keepopen_cb_toggled), task);

	hbox_buttons = gtk_hbox_new (FALSE, 0);
	btn_help = gtk_button_new_from_stock (GTK_STOCK_HELP);
	btn_ok = gtk_button_new_from_stock (GTK_STOCK_OK);
	btn_cancel = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
	gtk_signal_connect (GTK_OBJECT(btn_ok), "clicked", GTK_SIGNAL_FUNC(ui_btn_ok_cb_clicked), task);
	gtk_signal_connect (GTK_OBJECT(btn_help), "clicked", GTK_SIGNAL_FUNC(ui_btn_help_cb_clicked), win_main);
	gtk_signal_connect (GTK_OBJECT(btn_cancel), "clicked", GTK_SIGNAL_FUNC(ui_btn_cancel_cb_clicked), NULL);
	gtk_box_pack_start (GTK_BOX(hbox_buttons), btn_ok, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX(hbox_buttons), btn_help, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(hbox_buttons), btn_cancel, FALSE, FALSE, 0);

	vbox_main = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_main), hbox_command, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(hbox_command), vbox_cmdstack, TRUE, TRUE, 2);
	gtk_box_pack_start (GTK_BOX(vbox_main), hbox_options, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(hbox_options), vbox_optionstack, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_optionstack), chk_keepopen, FALSE, FALSE, 10);
	gtk_widget_set_size_request (hbox_command, 460, -1); //  enforce a minimum width

	gtk_box_pack_start (GTK_BOX(vbox_main), hbox_buttons, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER(win_main), vbox_main);
	gtk_container_set_border_width (GTK_CONTAINER(win_main), 8);
	
	gtk_window_set_position (GTK_WINDOW(win_main), GTK_WIN_POS_CENTER);
	gtk_widget_show_all (win_main);
	
	if (want_debug == TRUE) {   // useful for viewing keyval, keystring, keycode
		g_signal_connect (win_main, "key-press-event", G_CALLBACK(ski_key_press_details), 0);
	}
	
	g_timeout_add (5, on_loaded_timeout, win_main);  // 5 milliseconds
	if (keepopen == TRUE)
	{   // synchronize initial ui button state
		gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(chk_keepopen), TRUE );
		keepopen = !keepopen;
	}

	gtk_main();

	free(task);
	pathstrings_free();
	return(0);
}
