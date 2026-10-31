#include <gtk/gtk.h>
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace std;

// ============================================================
// STRUCTURES
// ============================================================

struct ProcessInfo
{
    int pid;
    string name;
    double cpu;
    double memory;
};

struct GUIData
{
    GtkWidget *cpuLabel;
    GtkWidget *memoryLabel;
    GtkWidget *diskLabel;
    GtkWidget *swapLabel;
    GtkWidget *totalMemoryLabel;
    GtkWidget *coresLabel;

    GtkWidget *topCPULabel;
    GtkWidget *topMemoryLabel;
    GtkWidget *uptimeLabel;
    GtkWidget *processCountLabel;
    GtkWidget *systemHealthLabel;
    GtkWidget *lastUpdatedLabel;

    GtkWidget *cpuHistoryArea;
    GtkWidget *memoryHistoryArea;

    GtkWidget *cpuStatusLabel;
    GtkWidget *memoryStatusLabel;
    GtkWidget *diskStatusLabel;
    GtkWidget *swapStatusLabel;

    GtkWidget *cpuProgressBar;
    GtkWidget *memoryProgressBar;
    GtkWidget *diskProgressBar;
    GtkWidget *swapProgressBar;

    vector<double> cpuHistory;
    vector<double> memoryHistory;
};

// ============================================================
// GLOBAL CPU STATE
// ============================================================

long long previousTotal = 0;
long long previousIdle = 0;

// ============================================================
// HELPER FUNCTIONS
// ============================================================

string getStatusText(double value)
{
    if (value < 0)
        return "Calculating";

    if (value < 70)
        return "Normal";

    if (value < 90)
        return "Warning";

    return "High";
}

string formatPercent(double value)
{
    if (value < 0)
        return "Calculating...";

    stringstream ss;
    ss << fixed << setprecision(1) << value << "%";
    return ss.str();
}

string getCurrentTimeText()
{
    time_t now = time(nullptr);
    struct tm *localTime = localtime(&now);

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", localTime);

    return string(buffer);
}

string getSystemHealthStatus(double cpu,
                             double memory,
                             double disk,
                             double swap)
{
    if (cpu > 90 ||
        memory > 90 ||
        disk > 90 ||
        swap > 90)
    {
        return "High";
    }

    if (cpu >= 70 ||
        memory >= 70 ||
        disk >= 70 ||
        swap >= 70)
    {
        return "Warning";
    }

    return "Normal";
}

void addHistorySample(vector<double> &history, double value)
{
    const int maxSamples = 60;

    if (value >= 0)
    {
        history.push_back(value);

        if ((int)history.size() > maxSamples)
        {
            history.erase(history.begin());
        }
    }
}

// ============================================================
// CPU USAGE
// ============================================================

double getCPUUsage()
{
    ifstream file("/proc/stat");

    if (!file.is_open())
        return 0.0;

    string cpu;

    long long user = 0;
    long long nice = 0;
    long long system = 0;
    long long idle = 0;
    long long iowait = 0;
    long long irq = 0;
    long long softirq = 0;
    long long steal = 0;

    file >> cpu
         >> user
         >> nice
         >> system
         >> idle
         >> iowait
         >> irq
         >> softirq
         >> steal;

    long long idleTime = idle + iowait;

    long long total =
        user +
        nice +
        system +
        idle +
        iowait +
        irq +
        softirq +
        steal;

    // First sample is only a baseline
    if (previousTotal == 0)
    {
        previousTotal = total;
        previousIdle = idleTime;

        return -1.0;
    }

    long long totalDifference =
        total - previousTotal;

    long long idleDifference =
        idleTime - previousIdle;

    previousTotal = total;
    previousIdle = idleTime;

    if (totalDifference <= 0)
        return 0.0;

    double usage =
        ((double)(totalDifference - idleDifference)
         / totalDifference) * 100.0;

    if (usage < 0)
        usage = 0;

    if (usage > 100)
        usage = 100;

    return usage;
}

// ============================================================
// MEMORY USAGE
// ============================================================

double getMemoryUsage()
{
    ifstream file("/proc/meminfo");

    if (!file.is_open())
        return 0.0;

    string name;
    long long total = 0;
    long long available = 0;
    long long value = 0;
    string unit;

    while (file >> name >> value >> unit)
    {
        if (name == "MemTotal:")
        {
            total = value;
        }
        else if (name == "MemAvailable:")
        {
            available = value;
        }
    }

    if (total == 0)
        return 0.0;

    return ((double)(total - available) / total) * 100.0;
}

// ============================================================
// DISK USAGE
// ============================================================

double getDiskUsage()
{
    FILE *pipe =
        popen("df / | tail -1 | awk '{print $5}'", "r");

    if (!pipe)
        return 0.0;

    char buffer[64] = {};

    if (fgets(buffer, sizeof(buffer), pipe) == nullptr)
    {
        pclose(pipe);
        return 0.0;
    }

    pclose(pipe);

    string value(buffer);

    if (!value.empty() && value.back() == '\n')
        value.pop_back();

    value.erase(
        remove_if(
            value.begin(),
            value.end(),
            [](unsigned char ch)
            {
                return ch == '%';
            }),
        value.end());

    try
    {
        return stod(value);
    }
    catch (...)
    {
        return 0.0;
    }
}

// ============================================================
// SWAP USAGE
// ============================================================

double getSwapUsage()
{
    ifstream file("/proc/meminfo");

    if (!file.is_open())
        return 0.0;

    string name;
    long long value = 0;
    string unit;

    long long swapTotal = 0;
    long long swapFree = 0;

    while (file >> name >> value >> unit)
    {
        if (name == "SwapTotal:")
        {
            swapTotal = value;
        }
        else if (name == "SwapFree:")
        {
            swapFree = value;
        }
    }

    if (swapTotal == 0)
        return 0.0;

    return ((double)(swapTotal - swapFree)
            / swapTotal) * 100.0;
}

// ============================================================
// TOTAL MEMORY
// ============================================================

string getTotalMemory()
{
    ifstream file("/proc/meminfo");

    if (!file.is_open())
        return "Unavailable";

    string name;
    long long value = 0;
    string unit;

    while (file >> name >> value >> unit)
    {
        if (name == "MemTotal:")
        {
            double gb =
                value / (1024.0 * 1024.0);

            stringstream ss;

            ss << fixed
               << setprecision(2)
               << gb
               << " GB";

            return ss.str();
        }
    }

    return "Unavailable";
}

// ============================================================
// CPU CORES
// ============================================================

int getCPUCores()
{
    long cores =
        sysconf(_SC_NPROCESSORS_ONLN);

    if (cores < 1)
        return 1;

    return (int)cores;
}

// ============================================================
// SYSTEM UPTIME
// ============================================================

string getUptime()
{
    ifstream file("/proc/uptime");

    if (!file.is_open())
        return "Unavailable";

    double seconds = 0;

    file >> seconds;

    int totalSeconds = (int)seconds;

    int hours =
        totalSeconds / 3600;

    int minutes =
        (totalSeconds % 3600) / 60;

    int secs =
        totalSeconds % 60;

    return to_string(hours)
           + " Hours "
           + to_string(minutes)
           + " Minutes "
           + to_string(secs)
           + " Seconds";
}

// ============================================================
// PROCESS COUNT
// ============================================================

int getProcessCount()
{
    FILE *pipe =
        popen("ps -e --no-headers | wc -l", "r");

    if (!pipe)
        return 0;

    char buffer[64] = {};

    fgets(buffer, sizeof(buffer), pipe);

    pclose(pipe);

    try
    {
        return stoi(string(buffer));
    }
    catch (...)
    {
        return 0;
    }
}

// ============================================================
// PROCESS MONITORING
// ============================================================

vector<ProcessInfo> getProcesses()
{
    vector<ProcessInfo> processes;

    FILE *pipe =
        popen(
            "ps -eo pid,comm,%cpu,%mem --no-headers",
            "r");

    if (!pipe)
        return processes;

    char buffer[512];

    while (fgets(buffer, sizeof(buffer), pipe))
    {
        string line(buffer);

        stringstream ss(line);

        ProcessInfo process;

        process.pid = 0;
        process.cpu = 0.0;
        process.memory = 0.0;

        ss >> process.pid
           >> process.name
           >> process.cpu
           >> process.memory;

        // Exclude this monitoring program itself
        if (process.pid > 0 &&
            process.pid != getpid())
        {
            processes.push_back(process);
        }
    }

    pclose(pipe);

    return processes;
}

// ============================================================
// CARD CREATION
// ============================================================

GtkWidget *createCard(
    const string &title,
    GtkWidget **valueLabel,
    GtkWidget **statusLabel,
    GtkWidget **progressBar)
{
    GtkWidget *frame =
        gtk_frame_new(nullptr);

    GtkWidget *box =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            8);

    gtk_widget_set_margin_top(box, 15);
    gtk_widget_set_margin_bottom(box, 15);
    gtk_widget_set_margin_start(box, 15);
    gtk_widget_set_margin_end(box, 15);

    GtkWidget *titleLabel =
        gtk_label_new(title.c_str());

    gtk_label_set_xalign(
        GTK_LABEL(titleLabel), 0.5);

    *valueLabel =
        gtk_label_new("Loading...");

    gtk_widget_set_halign(
        *valueLabel,
        GTK_ALIGN_CENTER);

    *statusLabel =
        gtk_label_new("Normal");

    gtk_widget_set_halign(
        *statusLabel,
        GTK_ALIGN_CENTER);

    *progressBar =
        gtk_progress_bar_new();

    gtk_progress_bar_set_show_text(
        GTK_PROGRESS_BAR(*progressBar),
        TRUE);

    gtk_progress_bar_set_text(
        GTK_PROGRESS_BAR(*progressBar),
        "0%");

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(*progressBar),
        0.0);

    gtk_widget_set_hexpand(
        *progressBar,
        TRUE);

    gtk_box_append(
        GTK_BOX(box),
        titleLabel);

    gtk_box_append(
        GTK_BOX(box),
        *valueLabel);

    gtk_box_append(
        GTK_BOX(box),
        *statusLabel);

    gtk_box_append(
        GTK_BOX(box),
        *progressBar);

    gtk_frame_set_child(
        GTK_FRAME(frame),
        box);

    return frame;
}

GtkWidget *createSimpleCard(
    const string &title,
    GtkWidget **valueLabel)
{
    GtkWidget *frame =
        gtk_frame_new(nullptr);

    GtkWidget *box =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            8);

    gtk_widget_set_margin_top(box, 15);
    gtk_widget_set_margin_bottom(box, 15);
    gtk_widget_set_margin_start(box, 15);
    gtk_widget_set_margin_end(box, 15);

    GtkWidget *titleLabel =
        gtk_label_new(title.c_str());

    *valueLabel =
        gtk_label_new("Loading...");

    gtk_box_append(
        GTK_BOX(box),
        titleLabel);

    gtk_box_append(
        GTK_BOX(box),
        *valueLabel);

    gtk_frame_set_child(
        GTK_FRAME(frame),
        box);

    return frame;
}

// ============================================================
// DRAWING GRAPH
// ============================================================

void drawHistoryGraph(
    GtkWidget *widget,
    cairo_t *cr,
    const vector<double> &history,
    const string &labelText)
{
    int width =
        gtk_widget_get_width(widget);

    int height =
        gtk_widget_get_height(widget);

    if (width <= 0 || height <= 0)
        return;

    // Background
    cairo_set_source_rgb(
        cr,
        0.06,
        0.10,
        0.16);

    cairo_paint(cr);

    double leftMargin = 40.0;
    double rightMargin = 15.0;
    double topMargin = 25.0;
    double bottomMargin = 25.0;

    double graphWidth =
        width -
        leftMargin -
        rightMargin;

    double graphHeight =
        height -
        topMargin -
        bottomMargin;

    // Grid
    cairo_set_line_width(cr, 1.0);

    cairo_set_source_rgba(
        cr,
        0.6,
        0.7,
        0.8,
        0.25);

    for (int i = 0; i <= 4; ++i)
    {
        double y =
            topMargin +
            graphHeight *
            (i / 4.0);

        cairo_move_to(
            cr,
            leftMargin,
            y);

        cairo_line_to(
            cr,
            width - rightMargin,
            y);
    }

    cairo_stroke(cr);

    // Y-axis labels
    cairo_set_source_rgb(
        cr,
        0.85,
        0.90,
        0.95);

    cairo_select_font_face(
        cr,
        "Sans",
        CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);

    cairo_set_font_size(cr, 10.0);

    cairo_move_to(
        cr,
        5,
        topMargin + 4);

    cairo_show_text(cr, "100%");

    cairo_move_to(
        cr,
        10,
        height - bottomMargin + 4);

    cairo_show_text(cr, "0%");

    // Graph title
    cairo_set_font_size(cr, 12.0);

    cairo_move_to(
        cr,
        leftMargin,
        16);

    cairo_show_text(
        cr,
        labelText.c_str());

    // No history yet
    if (history.empty())
        return;

    // Graph line
    cairo_set_source_rgb(
        cr,
        0.20,
        0.75,
        0.95);

    cairo_set_line_width(
        cr,
        3.0);

    if (history.size() == 1)
    {
        double value = history[0];

        double x =
            leftMargin +
            graphWidth / 2.0;

        double y =
            height -
            bottomMargin -
            (value / 100.0) *
                graphHeight;

        cairo_arc(
            cr,
            x,
            y,
            4.0,
            0,
            2 * M_PI);

        cairo_fill(cr);

        return;
    }

    for (size_t i = 1;
         i < history.size();
         ++i)
    {
        double x1 =
            leftMargin +
            graphWidth *
                ((double)(i - 1) /
                 (history.size() - 1));

        double x2 =
            leftMargin +
            graphWidth *
                ((double)i /
                 (history.size() - 1));

        double y1 =
            height -
            bottomMargin -
            (history[i - 1] / 100.0) *
                graphHeight;

        double y2 =
            height -
            bottomMargin -
            (history[i] / 100.0) *
                graphHeight;

        cairo_move_to(
            cr,
            x1,
            y1);

        cairo_line_to(
            cr,
            x2,
            y2);
    }

    cairo_stroke(cr);
}

// ============================================================
// GTK4 DRAW CALLBACKS
// ============================================================

static void drawCPUHistoryFunc(
    GtkDrawingArea *area,
    cairo_t *cr,
    int width,
    int height,
    gpointer data)
{
    (void)width;
    (void)height;

    GUIData *gui =
        static_cast<GUIData *>(data);

    drawHistoryGraph(
        GTK_WIDGET(area),
        cr,
        gui->cpuHistory,
        "CPU Usage History");
}

static void drawMemoryHistoryFunc(
    GtkDrawingArea *area,
    cairo_t *cr,
    int width,
    int height,
    gpointer data)
{
    (void)width;
    (void)height;

    GUIData *gui =
        static_cast<GUIData *>(data);

    drawHistoryGraph(
        GTK_WIDGET(area),
        cr,
        gui->memoryHistory,
        "Memory Usage History");
}

// ============================================================
// STATUS UPDATERS
// ============================================================

void updateResourceDisplay(
    GtkWidget *valueLabel,
    GtkWidget *statusLabel,
    GtkWidget *progressBar,
    double value,
    const string &labelText)
{
    if (value < 0)
    {
        gtk_label_set_text(
            GTK_LABEL(valueLabel),
            "Calculating...");

        gtk_label_set_text(
            GTK_LABEL(statusLabel),
            "Calculating");

        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(progressBar),
            0.0);

        gtk_progress_bar_set_text(
            GTK_PROGRESS_BAR(progressBar),
            "Calculating");

        return;
    }

    string valueText =
        labelText + formatPercent(value);

    gtk_label_set_text(
        GTK_LABEL(valueLabel),
        valueText.c_str());

    string health =
        getStatusText(value);

    gtk_label_set_text(
        GTK_LABEL(statusLabel),
        health.c_str());

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(progressBar),
        value / 100.0);

    string percentage =
        formatPercent(value);

    gtk_progress_bar_set_text(
        GTK_PROGRESS_BAR(progressBar),
        percentage.c_str());
}

void updateSimpleValue(
    GtkWidget *label,
    const string &text)
{
    gtk_label_set_text(
        GTK_LABEL(label),
        text.c_str());
}

// ============================================================
// UPDATE MONITOR
// ============================================================

gboolean updateMonitor(gpointer data)
{
    GUIData *gui =
        static_cast<GUIData *>(data);

    // --------------------------------------------------------
    // CPU
    // --------------------------------------------------------

    double cpu =
        getCPUUsage();

    if (cpu >= 0)
    {
        addHistorySample(
            gui->cpuHistory,
            cpu);
    }

    updateResourceDisplay(
        gui->cpuLabel,
        gui->cpuStatusLabel,
        gui->cpuProgressBar,
        cpu,
        "");

    // --------------------------------------------------------
    // MEMORY
    // --------------------------------------------------------

    double memory =
        getMemoryUsage();

    addHistorySample(
        gui->memoryHistory,
        memory);

    updateResourceDisplay(
        gui->memoryLabel,
        gui->memoryStatusLabel,
        gui->memoryProgressBar,
        memory,
        "");

    // --------------------------------------------------------
    // DISK
    // --------------------------------------------------------

    double disk =
        getDiskUsage();

    updateResourceDisplay(
        gui->diskLabel,
        gui->diskStatusLabel,
        gui->diskProgressBar,
        disk,
        "");

    // --------------------------------------------------------
    // SWAP
    // --------------------------------------------------------

    double swap =
        getSwapUsage();

    updateResourceDisplay(
        gui->swapLabel,
        gui->swapStatusLabel,
        gui->swapProgressBar,
        swap,
        "");

    // --------------------------------------------------------
    // TOTAL MEMORY
    // --------------------------------------------------------

    updateSimpleValue(
        gui->totalMemoryLabel,
        getTotalMemory());

    // --------------------------------------------------------
    // CPU CORES
    // --------------------------------------------------------

    string cores =
        to_string(getCPUCores()) +
        " Cores";

    updateSimpleValue(
        gui->coresLabel,
        cores);

    // --------------------------------------------------------
    // UPTIME
    // --------------------------------------------------------

    string uptime =
        "System Uptime: " +
        getUptime();

    updateSimpleValue(
        gui->uptimeLabel,
        uptime);

    // --------------------------------------------------------
    // PROCESS COUNT
    // --------------------------------------------------------

    string processCount =
        "Running Processes: " +
        to_string(getProcessCount());

    updateSimpleValue(
        gui->processCountLabel,
        processCount);

    // --------------------------------------------------------
    // SYSTEM HEALTH
    // --------------------------------------------------------

    string health =
        getSystemHealthStatus(
            cpu >= 0 ? cpu : 0,
            memory,
            disk,
            swap);

    updateSimpleValue(
        gui->systemHealthLabel,
        "System Health: " + health);

    // --------------------------------------------------------
    // LAST UPDATED
    // --------------------------------------------------------

    updateSimpleValue(
        gui->lastUpdatedLabel,
        "Last Updated: " +
            getCurrentTimeText());

    // --------------------------------------------------------
    // TOP PROCESSES
    // --------------------------------------------------------

    vector<ProcessInfo> processes =
        getProcesses();

    if (!processes.empty())
    {
        auto topCPU =
            max_element(
                processes.begin(),
                processes.end(),
                [](const ProcessInfo &a,
                   const ProcessInfo &b)
                {
                    return a.cpu < b.cpu;
                });

        string topCPUText =
            topCPU->name +
            "  —  " +
            formatPercent(topCPU->cpu) +
            " CPU";

        updateSimpleValue(
            gui->topCPULabel,
            topCPUText);

        auto topMemory =
            max_element(
                processes.begin(),
                processes.end(),
                [](const ProcessInfo &a,
                   const ProcessInfo &b)
                {
                    return a.memory < b.memory;
                });

        string topMemoryText =
            topMemory->name +
            "  —  " +
            formatPercent(topMemory->memory) +
            " Memory";

        updateSimpleValue(
            gui->topMemoryLabel,
            topMemoryText);
    }
    else
    {
        updateSimpleValue(
            gui->topCPULabel,
            "No active process available");

        updateSimpleValue(
            gui->topMemoryLabel,
            "No active process available");
    }

    // --------------------------------------------------------
    // REDRAW GRAPHS
    // --------------------------------------------------------

    gtk_widget_queue_draw(
        gui->cpuHistoryArea);

    gtk_widget_queue_draw(
        gui->memoryHistoryArea);

    return TRUE;
}

// ============================================================
// REFRESH BUTTON CALLBACK
// ============================================================

static void onRefreshClicked(
    GtkButton *button,
    gpointer data)
{
    (void)button;

    updateMonitor(data);
}

// ============================================================
// ABOUT DIALOG
// ============================================================

static void showAboutDialog(
    GtkButton *button,
    gpointer user_data)
{
    (void)button;
    (void)user_data;

    GtkWidget *dialog =
        gtk_window_new();

    gtk_window_set_title(
        GTK_WINDOW(dialog),
        "About Project");

    gtk_window_set_default_size(
        GTK_WINDOW(dialog),
        500,
        350);

    GtkWidget *box =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            12);

    gtk_widget_set_margin_top(
        box,
        20);

    gtk_widget_set_margin_bottom(
        box,
        20);

    gtk_widget_set_margin_start(
        box,
        20);

    gtk_widget_set_margin_end(
        box,
        20);

    GtkWidget *titleLabel =
        gtk_label_new(
            "Linux Resource Monitoring Tool");

    gtk_widget_set_halign(
        titleLabel,
        GTK_ALIGN_START);

    GtkWidget *aboutText =
        gtk_label_new(
            "Developed by:\n"
            "2520090008 - Sathish Kumar\n"
            "2520090016 - P. Rahul\n"
            "2520090090 - Sai Vihamsh\n\n"
            "Technology:\n"
            "C++\n"
            "GTK4\n"
            "Linux / Ubuntu / WSL\n"
            "/proc virtual filesystem\n\n"
            "Main Concepts:\n"
            "- CPU Monitoring\n"
            "- Memory Management\n"
            "- Disk Monitoring\n"
            "- Process Monitoring\n"
            "- Resource Management\n"
            "- Linux /proc filesystem");

    gtk_label_set_xalign(
        GTK_LABEL(aboutText),
        0.0);

    gtk_label_set_yalign(
        GTK_LABEL(aboutText),
        0.0);

    gtk_label_set_wrap(
        GTK_LABEL(aboutText),
        TRUE);

    GtkWidget *closeButton =
        gtk_button_new_with_label(
            "Close");

    g_signal_connect_swapped(
        closeButton,
        "clicked",
        G_CALLBACK(gtk_window_destroy),
        dialog);

    gtk_box_append(
        GTK_BOX(box),
        titleLabel);

    gtk_box_append(
        GTK_BOX(box),
        aboutText);

    gtk_box_append(
        GTK_BOX(box),
        closeButton);

    gtk_window_set_child(
        GTK_WINDOW(dialog),
        box);

    gtk_window_present(
        GTK_WINDOW(dialog));
}

// ============================================================
// GTK4 CSS THEME
// ============================================================

void applyTheme(GtkWidget *window)
{
    const char *css =

        "window {"
        "background-color: #f5f8ff;"
        "color: #1f2a44;"
        "}"

        "box {"
        "background-color: transparent;"
        "}"

        "label {"
        "color: #1f2a44;"
        "}"

        "button {"
        "background: #eaf1ff;"
        "color: #1f2a44;"
        "border-radius: 8px;"
        "padding: 8px 16px;"
        "}"

        "button:hover {"
        "background: #d8e6ff;"
        "}"

        "frame {"
        "background: #ffffff;"
        "border: 1px solid #d9e3f5;"
        "border-radius: 10px;"
        "}"

        "progressbar {"
        "min-height: 14px;"
        "}"

        "progressbar trough {"
        "background: #edf3ff;"
        "border-radius: 8px;"
        "}"

        "progressbar progress {"
        "background: #60a5fa;"
        "border-radius: 8px;"
        "}";

    GtkCssProvider *provider =
        gtk_css_provider_new();

    gtk_css_provider_load_from_string(
        provider,
        css);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

// ============================================================
// ACTIVATE
// ============================================================

static void activate(
    GtkApplication *app,
    gpointer user_data)
{
    (void)user_data;

    GtkWidget *window =
        gtk_application_window_new(app);

    gtk_window_set_title(
        GTK_WINDOW(window),
        "Linux Resource Monitoring Tool");

    gtk_window_set_default_size(
        GTK_WINDOW(window),
        1000,
        900);

    applyTheme(window);

    // --------------------------------------------------------
    // MAIN BOX
    // --------------------------------------------------------

    GtkWidget *mainBox =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            15);

    gtk_widget_set_margin_top(
        mainBox,
        25);

    gtk_widget_set_margin_bottom(
        mainBox,
        25);

    gtk_widget_set_margin_start(
        mainBox,
        30);

    gtk_widget_set_margin_end(
        mainBox,
        30);

    // --------------------------------------------------------
    // TITLE
    // --------------------------------------------------------

    GtkWidget *titleLabel =
        gtk_label_new(
            "LINUX RESOURCE MONITORING TOOL\n"
            "Real-Time System Monitor");

    gtk_label_set_justify(
        GTK_LABEL(titleLabel),
        GTK_JUSTIFY_CENTER);

    gtk_box_append(
        GTK_BOX(mainBox),
        titleLabel);

    // --------------------------------------------------------
    // RESOURCE ROW 1
    // --------------------------------------------------------

    GtkWidget *row1 =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            15);

    // --------------------------------------------------------
    // RESOURCE ROW 2
    // --------------------------------------------------------

    GtkWidget *row2 =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            15);

    GtkWidget *cpuValueLabel = nullptr;
    GtkWidget *memoryValueLabel = nullptr;
    GtkWidget *diskValueLabel = nullptr;
    GtkWidget *swapValueLabel = nullptr;

    GtkWidget *cpuStatusLabel = nullptr;
    GtkWidget *memoryStatusLabel = nullptr;
    GtkWidget *diskStatusLabel = nullptr;
    GtkWidget *swapStatusLabel = nullptr;

    GtkWidget *cpuProgressBar = nullptr;
    GtkWidget *memoryProgressBar = nullptr;
    GtkWidget *diskProgressBar = nullptr;
    GtkWidget *swapProgressBar = nullptr;

    // --------------------------------------------------------
    // CARDS
    // --------------------------------------------------------

    GtkWidget *cpuCard =
        createCard(
            "CPU USAGE",
            &cpuValueLabel,
            &cpuStatusLabel,
            &cpuProgressBar);

    GtkWidget *memoryCard =
        createCard(
            "MEMORY USAGE",
            &memoryValueLabel,
            &memoryStatusLabel,
            &memoryProgressBar);

    GtkWidget *diskCard =
        createCard(
            "DISK USAGE",
            &diskValueLabel,
            &diskStatusLabel,
            &diskProgressBar);

    GtkWidget *swapCard =
        createCard(
            "SWAP USAGE",
            &swapValueLabel,
            &swapStatusLabel,
            &swapProgressBar);

    GtkWidget *totalMemoryValueLabel =
        nullptr;

    GtkWidget *totalMemoryCard =
        createSimpleCard(
            "TOTAL MEMORY",
            &totalMemoryValueLabel);

    GtkWidget *coresValueLabel =
        nullptr;

    GtkWidget *coresCard =
        createSimpleCard(
            "CPU CORES",
            &coresValueLabel);

    // --------------------------------------------------------
    // EXPAND CARDS
    // --------------------------------------------------------

    gtk_widget_set_hexpand(
        cpuCard,
        TRUE);

    gtk_widget_set_hexpand(
        memoryCard,
        TRUE);

    gtk_widget_set_hexpand(
        diskCard,
        TRUE);

    gtk_widget_set_hexpand(
        swapCard,
        TRUE);

    gtk_widget_set_hexpand(
        totalMemoryCard,
        TRUE);

    gtk_widget_set_hexpand(
        coresCard,
        TRUE);

    // --------------------------------------------------------
    // ADD CARDS
    // --------------------------------------------------------

    gtk_box_append(
        GTK_BOX(row1),
        cpuCard);

    gtk_box_append(
        GTK_BOX(row1),
        memoryCard);

    gtk_box_append(
        GTK_BOX(row1),
        diskCard);

    gtk_box_append(
        GTK_BOX(row2),
        swapCard);

    gtk_box_append(
        GTK_BOX(row2),
        totalMemoryCard);

    gtk_box_append(
        GTK_BOX(row2),
        coresCard);

    gtk_box_append(
        GTK_BOX(mainBox),
        row1);

    gtk_box_append(
        GTK_BOX(mainBox),
        row2);

    // ========================================================
    // GRAPH SECTION
    // ========================================================

    GtkWidget *graphRow =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            20);

    GtkWidget *cpuGraphBox =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            8);

    GtkWidget *memoryGraphBox =
        gtk_box_new(
            GTK_ORIENTATION_VERTICAL,
            8);

    GtkWidget *cpuGraphTitle =
        gtk_label_new(
            "CPU Usage History");

    gtk_label_set_xalign(
        GTK_LABEL(cpuGraphTitle),
        0.0);

    GtkWidget *memoryGraphTitle =
        gtk_label_new(
            "Memory Usage History");

    gtk_label_set_xalign(
        GTK_LABEL(memoryGraphTitle),
        0.0);

    // --------------------------------------------------------
    // CPU DRAWING AREA
    // --------------------------------------------------------

    GtkWidget *cpuHistoryArea =
        gtk_drawing_area_new();

    gtk_widget_set_size_request(
        cpuHistoryArea,
        400,
        200);

    gtk_widget_set_hexpand(
        cpuHistoryArea,
        TRUE);

    // --------------------------------------------------------
    // MEMORY DRAWING AREA
    // --------------------------------------------------------

    GtkWidget *memoryHistoryArea =
        gtk_drawing_area_new();

    gtk_widget_set_size_request(
        memoryHistoryArea,
        400,
        200);

    gtk_widget_set_hexpand(
        memoryHistoryArea,
        TRUE);

    // IMPORTANT:
    // Do NOT make drawing areas pointer targets.
    // This keeps mouse handling normal.

    gtk_box_append(
        GTK_BOX(cpuGraphBox),
        cpuGraphTitle);

    gtk_box_append(
        GTK_BOX(cpuGraphBox),
        cpuHistoryArea);

    gtk_box_append(
        GTK_BOX(memoryGraphBox),
        memoryGraphTitle);

    gtk_box_append(
        GTK_BOX(memoryGraphBox),
        memoryHistoryArea);

    gtk_widget_set_hexpand(
        cpuGraphBox,
        TRUE);

    gtk_widget_set_hexpand(
        memoryGraphBox,
        TRUE);

    gtk_box_append(
        GTK_BOX(graphRow),
        cpuGraphBox);

    gtk_box_append(
        GTK_BOX(graphRow),
        memoryGraphBox);

    gtk_box_append(
        GTK_BOX(mainBox),
        graphRow);

    // ========================================================
    // TOP CPU
    // ========================================================

    GtkWidget *topCPUTitle =
        gtk_label_new(
            "🔥 TOP CPU CONSUMER");

    gtk_label_set_xalign(
        GTK_LABEL(topCPUTitle),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        topCPUTitle);

    GtkWidget *topCPULabel =
        gtk_label_new("Loading...");

    gtk_label_set_xalign(
        GTK_LABEL(topCPULabel),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        topCPULabel);

    // ========================================================
    // TOP MEMORY
    // ========================================================

    GtkWidget *topMemoryTitle =
        gtk_label_new(
            "🧠 TOP MEMORY CONSUMER");

    gtk_label_set_xalign(
        GTK_LABEL(topMemoryTitle),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        topMemoryTitle);

    GtkWidget *topMemoryLabel =
        gtk_label_new("Loading...");

    gtk_label_set_xalign(
        GTK_LABEL(topMemoryLabel),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        topMemoryLabel);

    // ========================================================
    // SYSTEM INFORMATION
    // ========================================================

    GtkWidget *uptimeLabel =
        gtk_label_new(
            "System Uptime: Loading...");

    gtk_label_set_xalign(
        GTK_LABEL(uptimeLabel),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        uptimeLabel);

    GtkWidget *processCountLabel =
        gtk_label_new(
            "Running Processes: Loading...");

    gtk_label_set_xalign(
        GTK_LABEL(processCountLabel),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        processCountLabel);

    GtkWidget *systemHealthLabel =
        gtk_label_new(
            "System Health: Loading...");

    gtk_label_set_xalign(
        GTK_LABEL(systemHealthLabel),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        systemHealthLabel);

    GtkWidget *lastUpdatedLabel =
        gtk_label_new(
            "Last Updated: --:--:--");

    gtk_label_set_xalign(
        GTK_LABEL(lastUpdatedLabel),
        0.0);

    gtk_box_append(
        GTK_BOX(mainBox),
        lastUpdatedLabel);

    // ========================================================
    // BUTTONS
    // ========================================================

    GtkWidget *buttonRow =
        gtk_box_new(
            GTK_ORIENTATION_HORIZONTAL,
            15);

    GtkWidget *refreshButton =
        gtk_button_new_with_label(
            "Refresh");

    GtkWidget *aboutButton =
        gtk_button_new_with_label(
            "About Project");

    gtk_box_append(
        GTK_BOX(buttonRow),
        refreshButton);

    gtk_box_append(
        GTK_BOX(buttonRow),
        aboutButton);

    gtk_box_append(
        GTK_BOX(mainBox),
        buttonRow);

    // ========================================================
    // SET MAIN WINDOW CHILD
    // ========================================================

    gtk_window_set_child(
        GTK_WINDOW(window),
        mainBox);

    // ========================================================
    // GUI DATA
    // ========================================================

    GUIData *gui =
        new GUIData;

    gui->cpuLabel =
        cpuValueLabel;

    gui->memoryLabel =
        memoryValueLabel;

    gui->diskLabel =
        diskValueLabel;

    gui->swapLabel =
        swapValueLabel;

    gui->totalMemoryLabel =
        totalMemoryValueLabel;

    gui->coresLabel =
        coresValueLabel;

    gui->topCPULabel =
        topCPULabel;

    gui->topMemoryLabel =
        topMemoryLabel;

    gui->uptimeLabel =
        uptimeLabel;

    gui->processCountLabel =
        processCountLabel;

    gui->systemHealthLabel =
        systemHealthLabel;

    gui->lastUpdatedLabel =
        lastUpdatedLabel;

    gui->cpuHistoryArea =
        cpuHistoryArea;

    gui->memoryHistoryArea =
        memoryHistoryArea;

    gui->cpuStatusLabel =
        cpuStatusLabel;

    gui->memoryStatusLabel =
        memoryStatusLabel;

    gui->diskStatusLabel =
        diskStatusLabel;

    gui->swapStatusLabel =
        swapStatusLabel;

    gui->cpuProgressBar =
        cpuProgressBar;

    gui->memoryProgressBar =
        memoryProgressBar;

    gui->diskProgressBar =
        diskProgressBar;

    gui->swapProgressBar =
        swapProgressBar;

    // ========================================================
    // DRAW CALLBACKS
    // ========================================================

    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(cpuHistoryArea),
        drawCPUHistoryFunc,
        gui,
        nullptr);

    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(memoryHistoryArea),
        drawMemoryHistoryFunc,
        gui,
        nullptr);

    // ========================================================
    // BUTTON CALLBACKS
    // ========================================================

    g_signal_connect(
        refreshButton,
        "clicked",
        G_CALLBACK(onRefreshClicked),
        gui);

    g_signal_connect(
        aboutButton,
        "clicked",
        G_CALLBACK(showAboutDialog),
        nullptr);

    // ========================================================
    // AUTO REFRESH
    // ========================================================

    g_timeout_add(
        2000,
        updateMonitor,
        gui);

    // Initial update
    updateMonitor(gui);

    // Show window
    gtk_window_present(
        GTK_WINDOW(window));
}

// ============================================================
// MAIN
// ============================================================

int main(
    int argc,
    char **argv)
{
    GtkApplication *app =
        gtk_application_new(
            "com.sathish.linuxmonitor",
            G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(
        app,
        "activate",
        G_CALLBACK(activate),
        nullptr);

    int status =
        g_application_run(
            G_APPLICATION(app),
            argc,
            argv);

    g_object_unref(app);

    return status;
}