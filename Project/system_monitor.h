#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <string>
#include <vector>

struct ProcessInfo {
    int pid;
    std::string name;
    double cpu;
    double memory;
};

std::string getStatusText(double value);
std::string formatPercent(double value);
std::string getCurrentTimeText();
std::string getSystemHealthStatus(double cpu, double memory, double disk, double swap);
void addHistorySample(std::vector<double>& history, double value);

double getCPUUsage();
double getMemoryUsage();
double getDiskUsage();
double getSwapUsage();
std::string getTotalMemory();
int getCPUCores();
std::string getUptime();
int getProcessCount();
std::vector<ProcessInfo> getProcesses();

#endif
