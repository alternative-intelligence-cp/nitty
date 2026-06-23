#include <gtk/gtk.h>
int main(int argc, char **argv) {
    gtk_init();
    GtkWidget *da = gtk_drawing_area_new();
    if (da) printf("GTK widget created successfully before app_run!\n");
    return 0;
}
