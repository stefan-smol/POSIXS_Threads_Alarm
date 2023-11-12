#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <math.h>

#define ALARM_ARRAY_SIZE 128 // Adjust the size of message and alarm_category as needed.

typedef struct alarm_tag{
    struct alarm_tag *link;
    int alarm_id;
    int seconds;
    time_t time; /* seconds from EPOCH */
    time_t timestamp;
    char alarm_category[ALARM_ARRAY_SIZE];
    char message[ALARM_ARRAY_SIZE];
    int alarm_time_group_number;
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
alarm_t *alarm_list = NULL;

void *alarm_thread(void *arg){
    alarm_t *alarm;
    int sleep_time;
    time_t now;
    int status;

    while (1){
        status = pthread_mutex_lock(&alarm_mutex);
        if (status != 0)
            err_abort(status, "Lock mutex");
        alarm = alarm_list;

        if (alarm == NULL){
            sleep_time = 1;
        }
        else{
            now = time(NULL);
            if (alarm->time <= now){
                sleep_time = 0;
                alarm_list = alarm->link;
                printf("(%d) %s\n", alarm->seconds, alarm->message);
                free(alarm);
            }
            else{
                sleep_time = alarm->time - now;
            }
        }
        status = pthread_mutex_unlock(&alarm_mutex);
        if (status != 0)
            err_abort(status, "Unlock mutex");
        if (sleep_time > 0){
            sleep(sleep_time);
        }
        else{
            sched_yield();
        }
    }
    return NULL; // This return will never be reached, but is here to satisfy the compiler.
}

int get_group_number(int time){
    // Calculate alarm_time_group_number and store it in the alarm structure
    return (int) ceil((double)time / 5.0);
}

int main(int argc, char *argv[]){
    int status;
    char line[ALARM_ARRAY_SIZE];
    alarm_t *alarm, **last, *next;
    pthread_t thread;
    pthread_t main_thread_id = pthread_self(); // Get the main thread ID

    int user_alarm_id; // hold the user inputted alarm ID for Cancel_Alarm.

    status = pthread_create(&thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort(status, "Create alarm thread");

    while (1){
        printf("alarm> ");
        if (fgets(line, sizeof(line), stdin) == NULL)
            exit(0);
        if (strlen(line) <= 1)
            continue;

        alarm = (alarm_t *)malloc(sizeof(alarm_t));
        if (alarm == NULL)
            errno_abort("Allocate alarm");

        // Ensure message does not exceed 128 characters
        if (strlen(line) > 127) {
            line[127] = '\0'; // Truncate the line if it's too long
        }

        // Parse input line into command format.
        if (sscanf(line, "Start_Alarm(%d): %d %63[^\n]", &alarm->alarm_id, &alarm->seconds, alarm->message) == 3){
            // Insert the new alarm into the list, maintaining sorted order by alarm_id.
            status = pthread_mutex_lock(&alarm_mutex);
            if (status != 0)
                err_abort(status, "Lock mutex");
            
            alarm->timestamp = time(NULL);
            alarm->time = time(NULL) + alarm->seconds;
            alarm->alarm_time_group_number = get_group_number(alarm->seconds);
            
            last = &alarm_list;
            next = *last;
            while (next != NULL){
                if (next->alarm_id >= alarm->alarm_id){
                    alarm->link = next;
                    *last = alarm;
                    break;
                }
                last = &next->link;
                next = next->link;
            }
            if (next == NULL){
                *last = alarm;
                alarm->link = NULL;
            }

            // After inserting, print the confirmation message
            printf("Alarm(%d) Inserted by Main Thread %lu Into Alarm List at %ld: %d %s\n",
                   alarm->alarm_id, (unsigned long)main_thread_id, alarm->timestamp, alarm->seconds, alarm->message);

            status = pthread_mutex_unlock(&alarm_mutex);
            if (status != 0)
                err_abort(status, "Unlock mutex");

        }
        else if (sscanf(line, "Replace_Alarm(%d): %d %63s %63[^\n]", &alarm->alarm_id, &alarm->seconds,alarm->alarm_category, alarm->message) == 4){
            int found = 0;
            status = pthread_mutex_lock(&alarm_mutex);
            if (status != 0)
                err_abort(status, "Lock mutex");
            // Loop through the list to find the matching alarm
            for (last = &alarm_list; (next = *last) != NULL; last = &next->link)
            {
                if (next->alarm_id == alarm->alarm_id)
                {
                    // Found the alarm to replace
                    next->seconds = alarm->seconds;
                    next->time = time(NULL) + alarm->seconds; // Set new time
                    strncpy(next->message, alarm->message, sizeof(next->message) - 1);
                    strncpy(next->alarm_category, alarm->alarm_category, sizeof(next->alarm_category) - 1);
                    next->message[sizeof(next->message) - 1] = '\0';               // Ensure null-termination
                    next->alarm_category[sizeof(next->alarm_category) - 1] = '\0'; // Ensure null-termination
                    found = 1;
                    break;
                }
            }
            status = pthread_mutex_unlock(&alarm_mutex);
            if (status != 0)
                err_abort(status, "Unlock mutex");
            if (!found)
            {
                // No alarm with the given ID was found
                fprintf(stderr, "Replace_Alarm: No alarm found with ID %d.\n", alarm->alarm_id);
            }
            // Free the temporary alarm structure since it's not added to the list
            free(alarm);
        }
        else if (sscanf(line, "Cancel_Alarm(%d)", &user_alarm_id) == 1){
            alarm_t *current, *prev = NULL;
            int found = 0;

            status = pthread_mutex_lock(&alarm_mutex);
            if (status != 0)
                err_abort(status, "Lock mutex");

            current = alarm_list;
            while (current != NULL)
            {
                if (current->alarm_id == user_alarm_id)
                {
                    found = 1;
                    if (prev == NULL)
                    {
                        // The alarm to cancel is at the head of the list
                        alarm_list = current->link;
                    }
                    else
                    {
                        // The alarm to cancel is in the middle or at the end of the list
                        prev->link = current->link;
                    }
                    free(current);
                    break;
                }
                prev = current;
                current = current->link;
            }

            status = pthread_mutex_unlock(&alarm_mutex);
            if (status != 0)
                err_abort(status, "Unlock mutex");

            if (!found)
            {
                // No alarm with the given ID was found
                fprintf(stderr, "Cancel_Alarm: No alarm found with ID %d.\n", user_alarm_id);
            }
            // Note that alarm is not used in this case, so free it immediately.
            free(alarm);
        }
        else{
            // Handle the bad command format.
            fprintf(stderr, "Bad command or format. Discarded: %s", line);
            free(alarm);
        }
    }
}