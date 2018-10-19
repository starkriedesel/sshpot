#include "auth.h"
#include "config.h"

#include <libssh/libssh.h>
#include <libssh/server.h>

#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>



/* Stores the current UTC time. Returns 0 on error. */
static int get_utc(struct connection *c) {
    time_t t;
    t = time(NULL);
    return strftime(c->con_time, MAXBUF, "%Y-%m-%d %H:%M:%S", gmtime(&t));
}


/* Stores the client's IP address in the connection sruct. */
static int *get_client_ip(struct connection *c) {
    struct sockaddr_storage tmp;
    struct sockaddr_in *sock;
    unsigned int len = MAXBUF;

    getpeername(ssh_get_fd(c->session), (struct sockaddr*)&tmp, &len);
    sock = (struct sockaddr_in *)&tmp;
    inet_ntop(AF_INET, &sock->sin_addr, c->client_ip, len);

    return 0;
}


/* Write interesting information about a connection attempt to  LOGFILE. 
 * Returns -1 on error. */
static int log_attempt(struct connection *c, int is_cve_2018_10933) {
    FILE *f;
    int r;

    if ((f = fopen(LOGFILE, "a+")) == NULL) {
        fprintf(stderr, "Unable to open %s\n", LOGFILE);
        return -1;
    }

    if (get_utc(c) <= 0) {
        fprintf(stderr, "Error getting time\n");
        return -1;
    }

    if (get_client_ip(c) < 0) {
        fprintf(stderr, "Error getting client ip\n");
        return -1;
    }

    if (is_cve_2018_10933) {
      if (DEBUG) { printf("%s %s CVE-2018-10933\n", c->con_time, c->client_ip); }
      r = fprintf(f, "%s %s CVE-2018-10933\n", c->con_time, c->client_ip);

    } else {
      c->user = ssh_message_auth_user(c->message);
      c->pass = ssh_message_auth_password(c->message);

      if (DEBUG) { printf("%s %s %s %s\n", c->con_time, c->client_ip, c->user, c->pass); }
      r = fprintf(f, "%s %s %s %s\n", c->con_time, c->client_ip, c->user, c->pass);
    }

    fclose(f);
    return r;
}


/* Logs password auth attempts. Always replies with SSH_MESSAGE_USERAUTH_FAILURE. */
int handle_auth(ssh_session session) {
    struct connection con;
    con.session = session;

    /* Perform key exchange. */
    if (ssh_handle_key_exchange(con.session)) {
        fprintf(stderr, "Error exchanging keys: `%s'.\n", ssh_get_error(con.session));
        return -1;
    }
    if (DEBUG) { printf("Successful key exchange.\n"); }

    /* Wait for a message, which should be an authentication attempt. Send the default
     * reply if it isn't. Log the attempt and quit. */
    while (1) {
        if ((con.message = ssh_message_get(con.session)) == NULL) {
            break;
        }

        /* Log the authentication request and disconnect. */
        if (ssh_message_subtype(con.message) == SSH_AUTH_METHOD_PASSWORD) {
            log_attempt(&con, 0);
        }
        else if (ssh_message_type(con.message) == 2 && ssh_message_subtype(con.message) == 1) {
            log_attempt(&con, 1);
        }
        else {
            if (DEBUG) { fprintf(stderr, "Not a password authentication attempt (type=%d, subtype=%d).\n", ssh_message_type(con.message), ssh_message_subtype(con.message)); }
        }

        /* Send the default message regardless of the request type. */
        ssh_message_reply_default(con.message);
        ssh_message_free(con.message);
    }

    if (DEBUG) { printf("Exiting child.\n"); }
    return 0;
}
