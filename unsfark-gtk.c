#include <gtk/gtk.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "unsfark.h"

static SFARKHANDLE		Sfark;
static pthread_t		ThreadId;
static unsigned char	StopFlag;

static GtkWidget *	MainWindow;
static GtkWidget *	StatusWindow;
static GtkWidget *	ProgressWindow;
static GtkWidget *	ButtonWindow;

static const char	GladeName[] = "Missing unsfark.glade";
static const char	Load[] = "sfArk file to convert to soundfont:";
static const char	Save[] = "Name of saved soundfont file:";
static const char	ErrorStr[] = "sfArk Error";

/****************** getLoadName() *****************
 * Get the user's choice of filename to load,
 * and copies it to specified buffer.
 */

static char * getLoadName(char * buffer)
{
	GtkWidget *			dialog;
	char *				filename;
	GtkFileFilter *	filter;

	dialog = gtk_file_chooser_dialog_new(&Load[0], GTK_WINDOW(MainWindow), GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "sfark");
	gtk_file_filter_add_pattern(filter, "*.sfark");
	gtk_file_filter_add_pattern(filter, "*.sfArk");
	gtk_file_filter_add_pattern(filter, "*.SFARK");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "all");
	gtk_file_filter_add_pattern(filter, "*.*");
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

	filename = 0;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
	{
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		strcpy(buffer, filename);
		g_free(filename);
	}

	gtk_widget_destroy(dialog);

	return (filename ? buffer : 0);
}

/****************** getSaveName() *****************
 * Get the user's choice of filename to save,
 * and copies it to specified buffer.
 */

static char * getSaveName(char * buffer)
{
	GtkWidget *dialog;
	char *	filename;

	dialog = gtk_file_chooser_dialog_new(&Save[0], GTK_WINDOW(MainWindow), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), buffer);

	filename = 0;
	if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		strcpy(buffer, filename);
		g_free(filename);
	}

	gtk_widget_destroy(dialog);

	return (filename ? buffer : 0);
}

/********************* display_error() ***********************
 * Displays an error msg box.
 */

static void display_error(const char * msg)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new(MainWindow ? GTK_WINDOW(MainWindow) : 0, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, msg);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	gtk_statusbar_push(GTK_STATUSBAR(StatusWindow), 0, ErrorStr);
}

/*********************** get_exe_path() **********************
 * Copies the path of this EXE into the passed buffer, and
 * trims off at the last '/'.
 *
 * RETURNS: Size of path in TCHARs.
 */

static unsigned long get_exe_path(char * buffer)
{
	char							linkname[64];
	register pid_t				pid;
	register unsigned long	offset;

	pid = getpid();
	snprintf(&linkname[0], sizeof(linkname), "/proc/%i/exe", pid);
	offset = readlink(&linkname[0], buffer, PATH_MAX);
	if (offset == (unsigned long)-1)
	{
		const char		*ptr;

		// Get the path to the user's home dir
		if (!(ptr = getenv("HOME"))) ptr = &GladeName[11+8];
		strcpy(buffer, ptr);
		offset = strlen(buffer);
	}
	else
	{
		offset = strlen(buffer);
		while (offset && buffer[offset - 1] != '/') --offset;
	}

	if (offset && buffer[offset - 1] != '/') buffer[offset++] = '/';

	buffer[offset] = 0;

	return offset;
}

static void create_window(void)
{
	register GtkBuilder *	builder;
	char							fn[PATH_MAX];

	strcpy(&fn[get_exe_path(&fn[0])], &GladeName[8]);

	builder = gtk_builder_new();
	MainWindow = 0;
	if (gtk_builder_add_from_file(builder, &fn[0], NULL))
	{
		MainWindow = GTK_WIDGET(gtk_builder_get_object(builder, "MainWindow"));
		StatusWindow = GTK_WIDGET(gtk_builder_get_object(builder, "statusbar"));
		ProgressWindow = GTK_WIDGET(gtk_builder_get_object(builder, "progressbar"));
		ButtonWindow = GTK_WIDGET(gtk_builder_get_object(builder, "load"));
		gtk_builder_connect_signals(builder, NULL);
	}

	g_object_unref(G_OBJECT(builder));
}

gboolean update_percent(unsigned int percent)
{
	gtk_progress_bar_set_fraction((struct GtkProgressBar *)ProgressWindow, (gdouble)percent / 10);
	return 0;
} 

gboolean update_done(int errCode)
{
	if (errCode < 0)
		display_error(SfarkErrMsg(Sfark, errCode));
	else if (errCode == 1)
		gtk_statusbar_push(GTK_STATUSBAR(StatusWindow), 0, "Successfully extracted soundfont.");
	else
		gtk_statusbar_push(GTK_STATUSBAR(StatusWindow), 0, "Aborted.");
	gtk_button_set_label(GTK_BUTTON(ButtonWindow), "Load");

	return 0;
} 

void * extract_thread(void * args)
{
	int				errCode;
	unsigned char	percent;

	errCode = 0;
	percent = 0;
	do
	{
		if (percent != SfarkPercent(Sfark))
		{
			percent = SfarkPercent(Sfark);
			g_idle_add((GSourceFunc)update_percent, (void *)percent);
		}
	} while (!StopFlag && !(errCode = SfarkExtract(Sfark)));

	SfarkClose(Sfark);
	g_idle_add((GSourceFunc)update_done, (void *)errCode);

	ThreadId = 0;

	return 0;
}

G_MODULE_EXPORT void do_load(GtkWidget *widget, gpointer data)
{
	char		*str;
	int			errCode;

	// If thread is running, just tell it to abort
	if (ThreadId)
	{
		gtk_statusbar_push(GTK_STATUSBAR(StatusWindow), 0, "Aborting...");
		StopFlag = 1;
		pthread_join(ThreadId, NULL);
	}
	else
	{
		errCode = 0;
		gtk_progress_bar_set_fraction((struct GtkProgressBar *)ProgressWindow, 0);

		// Let user pick the sfark file
		str = getLoadName(SfarkGetBuffer(Sfark));
		if (str && !(errCode = SfarkOpen(Sfark, str)))
		{
			// Let user choose the name for the soundfont file. Present him
			// with the original name initially
			str = getSaveName(SfarkGetBuffer(Sfark));
			if (str && !(errCode = SfarkBeginExtract(Sfark, str)))
			{
				// Start a thread to do the extraction, so we don't tie up this GUI thread
				StopFlag = 0;
				if (pthread_create(&ThreadId, 0, extract_thread, Sfark))
				{
					ThreadId = 0;
					display_error("Can't create thread");
				}
				else
				{
					gtk_statusbar_push(GTK_STATUSBAR(StatusWindow), 0, "Extracting the soundfont...");
					gtk_button_set_label(GTK_BUTTON(ButtonWindow), "Abort");
				}
			}
		}

		if (errCode < 0) display_error(SfarkErrMsg(Sfark, errCode));
		if (!ThreadId) SfarkClose(Sfark);
	}
}

G_MODULE_EXPORT void on_window_destroy(GtkWidget *object, gpointer user_data)
{
	gtk_main_quit();
}

int main(int argc, char *argv[])
{
	g_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();
	gtk_init(&argc, &argv);

	create_window();
	if (MainWindow)
	{
		gtk_widget_show(MainWindow);

		if (!(Sfark = SfarkAlloc()))
			display_error("Not enough RAM!");
		else
		{
			ThreadId = 0;

			gtk_main();

			if (ThreadId)
			{
				StopFlag = 1;
				pthread_join(ThreadId, NULL);
			}

			SfarkFree(Sfark);
		}
	}
	else
		display_error(&GladeName[0]);

	gdk_threads_leave();

	return 0;
}
