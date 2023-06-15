#include "time_t.h"

static char g_currentTimeStr[40];
static char g_currentDateStr[40];

long long timeInMilliseconds()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

int fm(int date, int month, int year) 
{
    int fmonth, leap;
    if ((year % 100 == 0) && (year % 400 != 0))
        leap = 0;
    else if (year % 4 == 0)
        leap = 1;
    else
        leap = 0;
    fmonth = 3 + (2 - leap) * ((month + 2) / (2 * month))+ (5 * month + month / 9) / 2;
    fmonth = fmonth % 7;
    return fmonth;
}


int get_year_today(struct tm tm)
{
    return tm.tm_year + 1900;
}

int get_mon_today(struct tm tm)
{
    return tm.tm_mon + 1;
}

int get_day_today(struct tm tm)
{
    return tm.tm_mday;
}

int get_hour_today(struct tm tm)
{
    return tm.tm_hour;
}

int get_min_today(struct tm tm)
{
    return tm.tm_min;
}

int get_sec_today(struct tm tm)
{
    return tm.tm_sec;
}

int day_of_week(int date, int month, int year) 
{
    int dayOfWeek;
    int YY = year % 100;
    int century = year / 100;
    dayOfWeek = 1.25 * YY + fm(date, month, year) + date - 2 * (century % 4);
    dayOfWeek = dayOfWeek % 7;
    if(dayOfWeek == 0)
    {
        return 7;
    }
    else
        return dayOfWeek;
}

// char *get_localtime_now()
// {
//  time_t rawtime;
//  struct tm *info;
//  time( &rawtime );
//  info = localtime( &rawtime );
//  return asctime(info);
// }

char *get_localtime_now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t rawtime;
    struct tm *info;
    time( &rawtime );
    info = localtime( &rawtime );
    sprintf(g_currentTimeStr, "%02d:%02d:%02d.%03d", info->tm_hour, info->tm_min, info->tm_sec, tv.tv_usec/1000);
    return g_currentTimeStr;
}

char* GetCurrentDate()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t rawtime;
    struct tm *info;
    time( &rawtime );
    info = localtime( &rawtime );
    sprintf(g_currentDateStr, "%02d-%02d-%02d", info->tm_mday, info->tm_mon, info->tm_year + 1900);
    return g_currentDateStr;
}