#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <math.h>
#include <unistd.h>

#define ALARM_ARRAY_SIZE 128 // Define a constant for the maximum size of alarm messages and categories.
// Based on assignment requirement to keep message to 128 character

// Structure definition for an alarm.
typedef struct alarm_struct
{
    struct alarm_struct *link;             // Pointer to next alarm in the list.
    int alarm_id;                          // Unique identifier for the alarm.
    int seconds;                           // Number of seconds to wait before the alarm.
    time_t time;                           // Time at which the alarm should go off, measured in seconds since the epoch.
    char alarm_category[ALARM_ARRAY_SIZE]; // Category of the alarm.
    char message[ALARM_ARRAY_SIZE];        // Message associated with the alarm.
    int alarm_time_group_number;           // Group number of the alarm based on its time.
    time_t next_display_time;              // Time for next display of the alarm message.
} alarm_t;

// Structure definition for display alarm threads.
typedef struct display_thread_struct
{
    pthread_t thread_id;                // POSIX thread identifier.
    int time_group_number;              // Group number that the thread is responsible for.
    struct display_thread_struct *next; // Pointer to the next thread in the list.
} display_thread_t;

// Mutexes for synchronizing access to shared resources.
pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t display_thread_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global pointers to the head of linked lists for alarms and display threads.
alarm_t *alarm_list = NULL;
display_thread_t *display_thread_list = NULL;

// Thread function for display alarm threads.
void *display_alarm_thread(void *arg)
{
    int time_group_number = *((int *)arg);
    free(arg);

    while (1)
    {
        pthread_mutex_lock(&alarm_mutex);

        time_t now = time(NULL);
        alarm_t *current = alarm_list;
        while (current != NULL)
        {
            if (current->alarm_time_group_number == time_group_number)
            {
                if (now >= current->next_display_time)
                {
                    printf("Alarm (%d) Printed by Alarm Thread %lu for Alarm_Time_Group_Number %d at %ld: %d %s\n",
                           current->alarm_id, (unsigned long)pthread_self(), time_group_number, now, current->seconds, current->message);
                    current->next_display_time = now + current->seconds; // Set the next display time.
                }
            }
            current = current->link;
        }

        pthread_mutex_unlock(&alarm_mutex);
        sleep(1); // Sleep for a second before checking again.
    }
    return NULL;
}

// Function to calculate the group number of an alarm based on its time.
int get_group_number(int time)
{
    return (int)ceil((double)time / 5.0); // Calculate and return the ceiling of time divided by 5.
}

// Function to manage the creation and addition of display threads.
void manage_display_threads(int group_number, int alarm_id, time_t now) {
    int status; // For storing return values of various functions, particularly pthread functions.

    int exists = 0;
    char alarm_message[ALARM_ARRAY_SIZE] = "";  // Array to store the alarm message.

    // Lock the mutex to ensure thread-safe access to the shared alarm list.
    // This is important to prevent concurrent access issues.
    status = pthread_mutex_lock( & alarm_mutex); // Lock the mutex to safely access the alarm list.
    if (status != 0) {
        err_abort(status, "Lock mutex"); // Abort if mutex lock fails.
    }

    // Find the alarm with the given ID to get its message.
    alarm_t *current_alarm = alarm_list;

    while (current_alarm != NULL) {
        if (current_alarm->alarm_id == alarm_id) {
            strncpy(alarm_message, current_alarm->message, sizeof(alarm_message) - 1);
            alarm_message[sizeof(alarm_message) - 1] = '\0';  // Ensure null termination.
            break;
        }
        //current_alarm = current_alarm->link;
    }

    pthread_mutex_unlock(&alarm_mutex);  // Unlock the alarm list mutex.

    pthread_mutex_lock(&display_thread_mutex);  // Lock the display thread list mutex.

    display_thread_t *current_thread = display_thread_list;
    while (current_thread != NULL) {
        if (current_thread->time_group_number == group_number) {
            exists = 1;
            break;
        }
        current_thread = current_thread->next;
    }

    if (!exists) {
        pthread_t new_thread;
        int *time_group = malloc(sizeof(int));
        *time_group = group_number;
        int status = pthread_create(&new_thread, NULL, display_alarm_thread, time_group);
        if (status != 0) {
            err_abort(status, "Create display alarm thread");
        }

        display_thread_t *new_display_thread = (display_thread_t *)malloc(sizeof(display_thread_t));
        if (new_display_thread == NULL) {
            errno_abort("Allocate display thread");
        }
        new_display_thread->thread_id = new_thread;
        new_display_thread->time_group_number = group_number;
        new_display_thread->next = display_thread_list;
        display_thread_list = new_display_thread;

        // Print the confirmation along with the alarm message.
        printf("Created New Display Alarm Thread %lu for Alarm_Time_Group_Number %d to Display Alarm(%d) at %ld: %d %s\n",
               (unsigned long)new_thread, group_number, alarm_id, now, current_alarm->seconds, alarm_message);
    }

    pthread_mutex_unlock(&display_thread_mutex);  // Unlock the display thread list mutex.
}

// Function to terminate display threads if their corresponding group becomes empty.
void terminate_display_thread_if_empty(int group_number, time_t now)
{
    int status; // For storing return values of various functions, particularly pthread functions.

    // Lock the mutex to ensure thread-safe access to the shared alarm list.
    // This is important to prevent concurrent access issues.
    status = pthread_mutex_lock(&alarm_mutex); // Lock the mutex to safely access the alarm list.
    if (status != 0)
    {
        err_abort(status, "Lock mutex"); // Abort if mutex lock fails.
    }

    status = pthread_mutex_lock(&display_thread_mutex); // Lock the mutex to safely access the display thread list.
    if (status != 0)
    {
        err_abort(status, "Lock mutex"); // Abort if mutex lock fails.
    }

    alarm_t *current_alarm = alarm_list; // Start from the head of the alarm list.
    int found = 0;                       // Flag to check if any alarm exists in the specified group.
    while (current_alarm != NULL)
    { // Iterate over the alarm list.

        if (current_alarm->alarm_time_group_number == group_number)
        {              // Check if an alarm belongs to the specified group.
            found = 1; // Set the flag if an alarm is found.
            break;     // Exit the loop as an alarm is found.
        }
        current_alarm = current_alarm->link; // Move to the next alarm in the list.
    }

    if (!found)
    {                                                                                // If no alarms are found in the group, terminate the corresponding display thread.
        display_thread_t *current_thread = display_thread_list, *prev_thread = NULL; // Pointers to traverse the display thread list.
        while (current_thread != NULL)
        { // Iterate over the display thread list.
            if (current_thread->time_group_number == group_number)
            { // Check if the thread belongs to the specified group.
                // Remove the thread from the list.
                if (prev_thread == NULL)
                {
                    display_thread_list = current_thread->next; // Remove the first thread in the list.
                }
                else
                {
                    prev_thread->next = current_thread->next; // Remove a thread in the middle or end of the list.
                }
                pthread_cancel(current_thread->thread_id); // Terminate the thread.
                free(current_thread);                      // Free the memory allocated for the thread structure.

                printf("Display Alarm Thread %lu for Alarm_Time_Group_Number %d Terminated at %ld\n",
                       (unsigned long)current_thread, group_number, now); // Print confirmation of termination.
                break;                     // Exit the loop as the thread has been terminated.
            }
            prev_thread = current_thread;          // Update the previous thread pointer.
            current_thread = current_thread->next; // Move to the next thread in the list.
        }

        status = pthread_mutex_unlock(&display_thread_mutex); // Unlock the display thread list mutex.
        if (status != 0)
        {
            err_abort(status, "Unlock mutex");
        }

        status = pthread_mutex_unlock(&alarm_mutex); // Unlock the alarm list mutex.
        if (status != 0)
        {
            err_abort(status, "Unlock mutex");
        }
    }
}

int main(int argc, char *argv[])
{
    // Variable declarations
    int status;                    // For storing return values of various functions, particularly pthread functions.
    char line[ALARM_ARRAY_SIZE];   // Buffer to store user input, limited by ALARM_ARRAY_SIZE.
    alarm_t *alarm, **last, *next; // Pointers for managing alarms in a linked list.

    // Store the thread ID of the main thread
    pthread_t main_thread_id = pthread_self();

    // Variable to store alarm ID parsed from user input for 'Cancel_Alarm' command
    int user_alarm_id;

    // Infinite loop to continuously accept and process user commands
    while (1)
    {
        printf("alarm> "); // Prompt for user input.

        // Read a line of input from the user and check for EOF (End Of File)
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            exit(0);
        } // Exit the program if EOF is encountered.

        // Skip processing if the input line is empty or only contains a newline character
        if (strlen(line) <= 1)
        {
            continue;
        }

        // Allocate memory for a new alarm structure
        alarm = (alarm_t *)malloc(sizeof(alarm_t));
        if (alarm == NULL)
        {
            errno_abort("Allocate alarm"); // Abort if memory allocation fails.
        }

        // Truncate input line if it's over 128 characters long
        if (strlen(line) > 127)
        {
            line[127] = '\0'; // Ensure the line ends with a null character.
        }

        // Parse input line into command format for Start_Alarm.
        if (sscanf(line, "Start_Alarm(%d): %d %63[^\n]", &alarm->alarm_id, &alarm->seconds, alarm->message) == 3)
        {

            // Lock the mutex to ensure thread-safe access to the shared alarm list.
            // This is important to prevent concurrent access issues.
            status = pthread_mutex_lock(&alarm_mutex);
            if (status != 0)
            {
                err_abort(status, "Lock mutex"); // Abort if mutex lock fails.
            }

            // Calculate the alarm time (current time plus the specified seconds).
            // This determines when the alarm should trigger.
            alarm->time = time(NULL) + alarm->seconds;
            alarm->next_display_time = alarm->time; // Set the next display time to the alarm time initially.

            // Calculate the alarm's group number based on its time,
            // grouping alarms into buckets of 5 seconds each.
            alarm->alarm_time_group_number = (int)ceil((double)alarm->seconds / 5.0);

            // Insert the new alarm into the alarm list in a sorted order by time.
            last = &alarm_list; // Start at the head of the list.
            next = *last;       // Initialize the next pointer for traversal.

            // Traverse the list to find the correct insertion point.
            while (next != NULL)
            {
                // If we find an alarm with a time greater or equal to the new alarm,
                // insert the new alarm before it to maintain sorted order.
                if (next->alarm_id >= alarm->alarm_id)
                {
                    alarm->link = next; // Point new alarm to the next one in the list.
                    *last = alarm;      // Insert the new alarm into the list.
                    break;              // Break as we have inserted the alarm.
                }
                // Move to the next alarm in the list.
                last = &next->link;
                next = next->link;
            }

            // If we reached the end of the list, append the new alarm at the end.
            if (next == NULL)
            {
                *last = alarm;      // Set the last alarm's link to the new alarm.
                alarm->link = NULL; // The new alarm is now the last one, so its link is NULL.
            }

            // Unlock the alarm list mutex so that manage_display_threads can access the alarm list.
            pthread_mutex_unlock( & alarm_mutex); // Unlock the alarm list mutex.
            if (status != 0) {
                err_abort(status, "Unlock mutex");
            }

            // Print a confirmation message indicating successful insertion.
            printf("Alarm(%d) Inserted by Main Thread %lu Into Alarm List at %ld: %d %s\n",
                   alarm->alarm_id, (unsigned long)main_thread_id, alarm->time, alarm->seconds, alarm->message);

            // After inserting the alarm into the list, manage display threads for this group number
            manage_display_threads(alarm->alarm_time_group_number, alarm->alarm_id, alarm->time);
        }
        else if (sscanf(line, "Replace_Alarm(%d): %d %63[^\n]", &alarm->alarm_id, &alarm->seconds, alarm->message) == 3)
        {
            int found = 0;           // Flag to check if the alarm is found in the list.
            time_t now = time(NULL); // Get the current time.

            // Lock the mutex to ensure exclusive access to the alarm list.
            status = pthread_mutex_lock(&alarm_mutex);
            if (status != 0)
            {
                err_abort(status, "Lock mutex");
            }

            // Iterate through the alarm list to find the alarm to be replaced.
            for (last = &alarm_list;
                 (next = *last) != NULL; last = &next->link)
            {
                if (next->alarm_id == alarm->alarm_id)
                {                                                                      // Check if the alarm IDs match.
                    int old_group_number = next->alarm_time_group_number;              // Store old group number for later use.
                    next->alarm_time_group_number = get_group_number(alarm->seconds);  // Recalculate the group number.
                    next->seconds = alarm->seconds;                                    // Update the seconds.
                    next->time = now + alarm->seconds;                                 // Update the alarm time.
                    next->next_display_time = next->time;                              // Update the next display time.
                    strncpy(next->message, alarm->message, sizeof(next->message) - 1); // Copy the new message.
                    next->message[sizeof(next->message) - 1] = '\0';                   // Ensure null termination.
                    found = 1;                                                         // Set the found flag.

                    // Print confirmation that the alarm has been replaced.
                    printf("Alarm(%d) Replaced at %ld: %d %s\n",
                           alarm->alarm_id, now, alarm->seconds, alarm->message);

                    // Unlock the alarm list mutex so that terminate_display_thread_if_empty can access the alarm list.
                    pthread_mutex_unlock(&alarm_mutex); // Unlock the alarm list mutex.
                    if (status != 0)
                    {
                        err_abort(status, "Unlock mutex");
                    }

                    // Manage display threads for the new group number and check if old threads need to be terminated.
                    manage_display_threads(next->alarm_time_group_number, alarm->alarm_id, now);
                    terminate_display_thread_if_empty(old_group_number, now);
                    break; // Break the loop as the alarm has been found and replaced.
                }
            }

            // Unlock the mutex after modifications.
            // status = pthread_mutex_unlock( & alarm_mutex);
            /*
            if (status != 0){
                err_abort(status, "Unlock mutex");
            }
            */

            // If the alarm ID was not found in the list, print an error message.
            if (!found)
            {
                fprintf(stderr, "Replace_Alarm: No alarm found with ID %d.\n", alarm->alarm_id);
            }
            free(alarm); // Free the memory allocated for the alarm.
        }
        else if (sscanf(line, "Cancel_Alarm(%d)", &user_alarm_id) == 1)
        {
            alarm_t *current, *prev = NULL; // Pointers to traverse and keep track of previous alarm.
            int found = 0;                  // Flag to check if the alarm is found in the list.
            int group_number = -1;          // To store the group number of the cancelled alarm.

            // Lock the mutex to ensure exclusive access to the alarm list.
            status = pthread_mutex_lock(&alarm_mutex);
            if (status != 0)
            {
                err_abort(status, "Lock mutex");
            }

            // Iterate through the alarm list to find and remove the specified alarm.
            current = alarm_list;
            while (current != NULL)
            {
                if (current->alarm_id == user_alarm_id)
                {                                                    // Check if the alarm IDs match.
                    found = 1;                                       // Set the found flag.
                    group_number = current->alarm_time_group_number; // Store the group number.
                    if (prev == NULL)
                    {
                        alarm_list = current->link; // Remove the alarm from the list.
                    }
                    else
                    {
                        prev->link = current->link; // Remove the alarm from the middle or end of the list.
                    }
                    time_t cancel_time = time(NULL); // Get the current time for the cancellation message.
                    printf("Alarm(%d) Canceled at %ld: %d %s\n", user_alarm_id, cancel_time, current->seconds, current->message);
                    free(current); // Free the memory allocated for the alarm.
                    break;         // Break the loop as the alarm has been found and cancelled.
                }
                prev = current;          // Update the previous pointer.
                current = current->link; // Move to the next alarm in the list.
            }

            // Unlock the mutex after modifications.
            pthread_mutex_unlock(&alarm_mutex);
            if (status != 0)
            {
                err_abort(status, "Unlock mutex");
            }

            // If the alarm ID was not found in the list, print an error message.
            if (found)
            {
                // Check if any other alarm exists in the same group and manage display threads accordingly.
                terminate_display_thread_if_empty(group_number, time(NULL));
            }
            else
            {
                fprintf(stderr, "Cancel_Alarm: No alarm found with ID %d.\n", user_alarm_id);
            }
            free(alarm); // Free the memory allocated for the alarm structure, even if not used.
        }
        else
        {
            // This block handles the case where the user input does not match any of the expected command formats.
            // If the input line does not match the format for starting, replacing, or canceling an alarm,
            // it is considered a bad command or format.

            // Print an error message to the standard error stream.
            fprintf(stderr, "Bad command or format. Discarded: %s", line);

            // Free the memory allocated for the alarm structure.
            free(alarm);
        }
        // End of the while loop. The program will go back to the beginning of the loop and wait for new user input.
    }
    // End of the main function.
}