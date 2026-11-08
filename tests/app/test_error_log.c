#include <string.h>

#include "check.h"
#include "error_log.h"
#include "fake_hal.h"

static ClockStore clock;

static void start(bool can_make_file)
{
    fake_hal_reset();
    fake_hal_set_reserve_result(can_make_file ? HAL_RESERVE_CREATED : HAL_RESERVE_FAILED);
    error_log_forget();
    error_log_use_clock(NULL);
}

static const char *text(void)
{
    uint32_t size = 0;
    return (const char *)fake_hal_file_bytes(ERROR_LOG_PATH, &size);
}

static void a_line_has_the_time_since_start_the_problem_and_the_path(void)
{
    start(true);
    fake_hal_set_milliseconds(3723000);
    error_log_add("can't open", "/Music/A.flac");
    CHECK(strcmp("+1:02:03  can't open: /Music/A.flac\n", text()) == 0);
}

static void lines_follow_each_other_and_a_restart_finds_the_end(void)
{
    start(true);
    fake_hal_set_milliseconds(0);
    error_log_add("one", "/a");
    error_log_forget();
    error_log_add("two", "/b");
    CHECK(strcmp("+0:00:00  one: /a\n+0:00:00  two: /b\n", text()) == 0);
}

static void the_time_comes_from_the_clock_once_it_is_set(void)
{
    start(true);
    Settings settings = settings_defaults();
    clock = (ClockStore){0};
    clock_store_start(&clock, &settings, 0);
    WallCivil civil = {2026, 11, 8, 21, 5, 0};
    clock_store_set(&clock, &settings, &civil, WALL_CLOCK_ZONE_UTC, 0);
    error_log_use_clock(&clock);
    error_log_add("can't open", "/x");
    CHECK(strcmp("2026-11-08 21:05  can't open: /x\n", text()) == 0);
}

static void add_numbered(uint32_t number)
{
    char path[200];
    memset(path, 'p', sizeof path - 1);
    path[sizeof path - 1] = '\0';
    for (int digit = 0; digit < 4; digit++) {
        path[3 - digit] = (char)('0' + number % 10);
        number /= 10;
    }
    error_log_add("can't open", path);
}

static void the_log_grows_with_every_line(void)
{
    start(true);
    fake_hal_set_milliseconds(0);
    error_log_add("one", "/a");
    uint32_t size = 0;
    fake_hal_file_bytes(ERROR_LOG_PATH, &size);
    CHECK_EQUAL(strlen("+0:00:00  one: /a\n"), size);
    for (uint32_t number = 0; number < 300; number++) {
        add_numbered(number);
        if (number % 50 == 0) {
            error_log_forget();
        }
    }
    const char *log = text();
    fake_hal_file_bytes(ERROR_LOG_PATH, &size);
    CHECK_EQUAL(strlen(log), size);
    CHECK(strncmp("+0:00:00  one: /a\n", log, 18) == 0);
}

static void the_same_error_twice_in_a_row_is_logged_once(void)
{
    start(true);
    fake_hal_set_milliseconds(0);
    error_log_add("can't open", "/a");
    error_log_add("can't open", "/a");
    error_log_add("can't open", "/b");
    error_log_add("can't open", "/a");
    CHECK(strcmp("+0:00:00  can't open: /a\n+0:00:00  can't open: /b\n"
                 "+0:00:00  can't open: /a\n",
                 text()) == 0);
}

static void a_session_logs_at_most_64_errors_and_says_so(void)
{
    start(true);
    fake_hal_set_milliseconds(0);
    for (uint32_t number = 0; number < 100; number++) {
        add_numbered(number);
    }
    const char *log = text();
    int count = 0;
    for (const char *at = log; *at != '\0'; at++) {
        count += *at == '\n';
    }
    CHECK_EQUAL(ERROR_LOG_SESSION_LINES + 1, count);
    CHECK(strstr(log, "too many errors, the rest of this session isn't logged\n") != NULL);
    int writes = fake_hal_write_at_count();
    add_numbered(500);
    CHECK_EQUAL(writes, fake_hal_write_at_count());
}

static void a_failed_write_stops_the_log_until_the_next_start(void)
{
    start(true);
    fake_hal_fail_file_writes(true);
    error_log_add("can't open", "/a");
    CHECK_EQUAL(1, fake_hal_write_at_count());
    error_log_add("can't open", "/b");
    error_log_add("can't open", "/c");
    CHECK_EQUAL(1, fake_hal_write_at_count());
    fake_hal_fail_file_writes(false);
    error_log_forget();
    error_log_add("can't open", "/d");
    CHECK_EQUAL(2, fake_hal_write_at_count());
}

static void line_breaks_in_a_path_are_replaced(void)
{
    start(true);
    fake_hal_set_milliseconds(0);
    error_log_add("can't open", "/a\nb");
    CHECK(strcmp("+0:00:00  can't open: /a?b\n", text()) == 0);
}

static void without_room_for_the_file_nothing_is_written(void)
{
    start(false);
    error_log_add("can't open", "/a");
    CHECK(text() == NULL);
    CHECK_EQUAL(0, fake_hal_write_count());
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_line_has_the_time_since_start_the_problem_and_the_path),
        TEST_CASE(lines_follow_each_other_and_a_restart_finds_the_end),
        TEST_CASE(the_time_comes_from_the_clock_once_it_is_set),
        TEST_CASE(the_log_grows_with_every_line),
        TEST_CASE(the_same_error_twice_in_a_row_is_logged_once),
        TEST_CASE(a_session_logs_at_most_64_errors_and_says_so),
        TEST_CASE(a_failed_write_stops_the_log_until_the_next_start),
        TEST_CASE(line_breaks_in_a_path_are_replaced),
        TEST_CASE(without_room_for_the_file_nothing_is_written),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
