#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define BILLION 1000000000L

long long timeInMilliseconds();
int fm(int date, int month, int year);
int get_year_today(struct tm tm);
int get_mon_today(struct tm tm);
int get_day_today(struct tm tm);
int get_hour_today(struct tm tm);
int get_min_today(struct tm tm);
int get_sec_today(struct tm tm);
int day_of_week(int date, int month, int year);
char *get_localtime_now();