#include <mxt_platform.h>
#include <mxt_common.h>
#include <mxt_lib.h>

/*
 *
 * Copyright (c) 2010, Symantec Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of Symantec Corporation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */


extern char * mux_get_log_entry();
extern int is_running_in_dom0();

#define AUTH_POLL_TIMEOUT	1000

#define MAX_RETRY_COUNT	3
int
mux_initiate_authorization(sendfd, auth)
	SOCKET		sendfd;
	struct mux_auth	*auth;
{
	int shmid;
	key_t key;
	char *shm, *s;
	int	index, remaining;
	char	buf[COOKIESZ];
	int	fd, ret;
	struct pollfd pollfd[1];
	int	poll_timeout = AUTH_POLL_TIMEOUT * 3;
	int	retry_count = 0;
	int	key_num = 0;

	/*
	 * We'll name our shared memory segment. Generate a random number
	 */
	fd = open("/dev/urandom", O_RDONLY);
	if (fd <= 0) {
		MUX_LOG_ENTRY0("open failed ");
		return -1;
	}
	ret = read(fd, (char *)&key_num, sizeof(key_num));
	if (ret != sizeof(key_num)) {
		MUX_LOG_ENTRY0("Generate key for authorization failed");
		return -1;
	}

	key = key_num;

	/*
	 * Create the segment.
	 */
	if ((shmid = shmget(key, SHMSZ, IPC_CREAT | 0)) < 0) {
		MUX_LOG_ENTRY0("shmget failed ");
		return -1;
	}

	/*
	 * Now we attach the segment to our data space.
	 */
	if ((shm = (char *)shmat(shmid, NULL, 0)) ==
	    (char *) -1) {
		MUX_LOG_ENTRY0("shmat failed ");
		shmctl(shmid, IPC_RMID, 0);
		return -1;
	}

	/*
	 * Now put some things into the memory for the
	 * other process to read.
	 */

	s = shm;
	index = 0;
	remaining = 128;
	while (remaining) {
		memset((void *)buf, '\0', 128);
		ret = read(fd, buf, remaining);
		if (ret) {
			memcpy(s + index, buf, ret);
			index += ret;
			remaining -= ret;
		}
	}
	s[128] = '\0';
	close(fd);

	memset(auth, '\0', sizeof(struct mux_auth));
	auth->mux_auth_fd = sendfd;
	auth->mux_auth_magic = READ_MAGIC;
	auth->mux_auth_key = key;

	ret = send(sendfd, auth, sizeof(struct mux_auth), 0);
	if (ret <= 0) {
		MUX_LOG_ENTRY0("Send failed ");
		ret = -1;
		goto exit;
	}

	pollfd[0].fd = sendfd;
	pollfd[0].events = POLLIN;
	pollfd[0].revents = 0;
retry:
	ret = poll(pollfd, 1, poll_timeout);
	if (ret <= 0) {
		MUX_LOG_ENTRY0("mux_initiate_authorization poll returned");
		if (++retry_count < MAX_RETRY_COUNT) {
			goto retry;
		} else {
			MUX_LOG_ENTRY0("Authorization failed ");
			ret = -1;
			goto exit;
		}
	}

	if (pollfd[0].revents != 0) {
		ret = recv(sendfd, buf, sizeof(buf), 0);
		if (ret <= 0) {
			MUX_LOG_ENTRY2("recv failed ret : %x errno : %x", ret,
				errno);
			ret = -1;
			goto exit;
		} else {
			if (memcmp(s, buf, 128) == 0) {
				MUX_LOG_ENTRY0("Authorization completed");
				ret = 0;
				goto exit;
			}
		}
	}

exit:
	shmdt(shm);
	shmctl(shmid, IPC_RMID, 0);

	return ret;
}

int
mux_respond_to_authorization(recvfd, auth)
	SOCKET	recvfd;
	struct mux_auth	*auth;
{
	int	shmid;
	key_t	key;
	char	*shm;
	struct pollfd pollfd[1];
	int	ret;
	char	recvbuf[20];
	struct	mux_auth	*recv_auth;
	int	poll_timeout = AUTH_POLL_TIMEOUT * 3;
	int	retry_count = 0;

	pollfd[0].fd = recvfd;
	pollfd[0].events = POLLIN;
	pollfd[0].revents = 0;

retry:
	ret = poll(pollfd, 1, poll_timeout);
	if (ret <= 0) {
		MUX_LOG_ENTRY0("mux_respond_to_authorization: poll returned");
		if (++retry_count < MAX_RETRY_COUNT) {
			goto retry;
		} else {
			MUX_LOG_ENTRY0("Authorization failed ");
			return -1;
		}
	}
	if (pollfd[0].revents != 0) {
		ret = recv(recvfd, recvbuf, sizeof(struct mux_auth), 0);
		if (ret <= 0) {
			MUX_LOG_ENTRY0("recv failed ");
			return -1;
		} else {
			recv_auth = (struct mux_auth *)recvbuf;
			if (recv_auth->mux_auth_magic == READ_MAGIC) {
				MUX_LOG_ENTRY0("Received msg READDATA");
			} else {
				return -1;
			}
		}
	} else {
		return -1;
	}

	key = recv_auth->mux_auth_key;

	/*
	 * Locate the segment.
	 */
	if ((shmid = shmget(key, SHMSZ, 0)) < 0) {
		MUX_LOG_ENTRY0("shmget failed ");
		return -1;
	}

	/*
	 * Now we attach the segment to our data space.
	 */
	if ((shm = (char *) shmat(shmid, NULL, 0)) == (char *) -1) {
		MUX_LOG_ENTRY0("shmat failed ");
		shmctl(shmid, IPC_RMID, 0);
		return -1;
	}

	ret = send(recvfd, shm, COOKIESZ, 0);
	if (ret <= 0) {
		MUX_LOG_ENTRY0("Send failed ");
		ret = -1;
		goto exit;
	}

	*auth = *recv_auth;

exit:
	shmdt(shm);
	shmctl(shmid, IPC_RMID, 0);
	return ret;
}
