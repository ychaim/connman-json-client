/*
 *  connman-json-client
 *
 *  Simple main loop for dbus (and stdin).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <poll.h>
#include <dbus/dbus.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "loop.h"

extern DBusConnection *connection;
extern void __ncurses_action(void);
static int stop_loop = 0;
static DBusWatch *watcheds[WATCHEDS_MAX_COUNT];
static int watcheds_count;

static dbus_bool_t add_watch(DBusWatch *watch, void *data)
{
	if (watcheds_count >= WATCHEDS_MAX_COUNT)
		return 0;
	
	watcheds[watcheds_count++] = watch;
	return 1;
}

static void remove_watch(DBusWatch *watch, void *data)
{
	int i, n;

	n = watcheds_count;

	for (i = 0; i < n; i++) {
		if (watcheds[i] == watch) {
			watcheds_count = --n;
			watcheds[i] = watcheds[watcheds_count];
			break;
		}
	}
}

void loop_init(void)
{
	dbus_connection_set_watch_functions(connection, add_watch, remove_watch,
			NULL, NULL, NULL);
}

void loop_terminate(void)
{
	dbus_connection_unref(connection);
	connection = 0;
	watcheds_count = 0;
}

void loop_quit(void)
{
	stop_loop = 1;
}

void loop_run(_Bool poll_stdin)
{
	struct pollfd fds[WATCHEDS_MAX_COUNT];
	DBusWatch *tmp_watcher;
	int nfds, i, status, processdbus;
	unsigned int flags;
	short revents, cond;

	while (!stop_loop) {

		nfds = 0;

		// prepare
		for (i = 0 ; i < watcheds_count ; i++) {
			tmp_watcher = watcheds[i];

			if (dbus_watch_get_enabled(tmp_watcher)) {
				cond = POLLHUP | POLLERR;
				flags = dbus_watch_get_flags(tmp_watcher);

				if (flags & DBUS_WATCH_READABLE) 
					cond |= POLLIN;

				watcheds[nfds] = tmp_watcher;
				fds[nfds].fd =
					dbus_watch_get_unix_fd(tmp_watcher);
				fds[nfds].events = cond;

				nfds++;
			}
		}

		if (poll_stdin) {
			fds[nfds].fd = fileno(stdin);
			fds[nfds].events = POLLHUP | POLLERR | POLLIN;
			nfds++;
		}


		// poll
		status = poll(fds, (nfds_t) nfds, -1);
		if (status <= 0) {
			printf("\n[-] poll error %d:%s\n", errno,
					strerror(errno));
			break;
		}

		// process
		processdbus = 0;

		if (poll_stdin)
			nfds--;

		for (i = 0; i < nfds; ++i) {
			revents = fds[i].revents;
			if (revents) {
				flags = 0;

				if (revents & POLLIN)  
					flags |= DBUS_WATCH_READABLE;

				if (revents & POLLHUP) 
					flags |= DBUS_WATCH_HANGUP;

				if (revents & POLLERR) 
					flags |= DBUS_WATCH_ERROR;

				dbus_watch_handle(watcheds[i], flags);
				processdbus = 1;
			}
		}

		if (processdbus) {
			while (dbus_connection_dispatch(connection) ==
					DBUS_DISPATCH_DATA_REMAINS);
		}
		
		if (poll_stdin && fds[nfds].revents & POLLIN)
			__ncurses_action();

	} // end while
	stop_loop = 0;
}
