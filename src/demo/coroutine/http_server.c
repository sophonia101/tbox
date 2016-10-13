/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */ 
#include "../demo.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */ 

// port
#define TB_DEMO_PORT        (8080)

// timeout
#define TB_DEMO_TIMEOUT     (-1)

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */ 

// the http session type
typedef struct __tb_demo_http_session_t
{
    // the socket
    tb_socket_ref_t sock;

    // the http code
    tb_size_t       code;

    // the http method
    tb_size_t       method;

    // the content size
    tb_hize_t       content_size;

    // is keep-alive?
    tb_bool_t       keep_alive;

    // the file
    tb_file_ref_t   file;

    // the data buffer
    tb_byte_t       data[8192];

    // the resource path
    tb_char_t       path[1024];

    // the line buffer
    tb_char_t       line[1024];

    // the line size
    tb_size_t       line_size;

    // the line index
    tb_size_t       line_index;

}tb_demo_http_session_t, *tb_demo_http_session_ref_t;

/* //////////////////////////////////////////////////////////////////////////////////////
 * globals
 */ 

// the root directory
static tb_char_t    g_roordir[TB_PATH_MAXN];

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */ 
static tb_bool_t tb_demo_http_session_init(tb_demo_http_session_ref_t session, tb_socket_ref_t sock)
{
    // check
    tb_assert_and_check_return(session && sock);

    // init session
    tb_memset(session, 0, sizeof(tb_demo_http_session_t));

    // init socket
    session->sock = sock;

    // ok
    return tb_true;
}
static tb_void_t tb_demo_http_session_exit(tb_demo_http_session_ref_t session)
{
    // check
    tb_assert_and_check_return(session);

    // exit socket
    if (session->sock) tb_socket_exit(session->sock);

    // exit file
    if (session->file) tb_file_exit(session->file);
}
static tb_bool_t tb_demo_http_session_data_send(tb_socket_ref_t sock, tb_byte_t* data, tb_size_t size)
{
    // check
    tb_assert_and_check_return_val(sock && data && size, tb_false);

    // send data
    tb_size_t send = 0;
    tb_long_t wait = 0;
    while (send < size)
    {
        // send it
        tb_long_t real = tb_socket_send(sock, data + send, size - send);

        // has data?
        if (real > 0) 
        {
            send += real;
            wait = 0;
        }
        // no data? wait it
        else if (!real && !wait)
        {
            // wait it
            wait = tb_socket_wait(sock, TB_SOCKET_EVENT_SEND, TB_DEMO_TIMEOUT);
            tb_assert_and_check_break(wait >= 0);
        }
        // failed or end?
        else break;
    }

    // ok?
    return send == size;
}
static tb_bool_t tb_demo_http_session_file_send(tb_socket_ref_t sock, tb_file_ref_t file)
{
    // check
    tb_assert_and_check_return_val(sock && file, tb_false);

    // send data
    tb_hize_t send = 0;
    tb_hize_t size = tb_file_size(file);
    tb_long_t wait = 0;
    while (send < size)
    {
        // send it
        tb_hong_t real = tb_socket_sendf(sock, file, send, size - send);

        // has data?
        if (real > 0) 
        {
            send += real;
            wait = 0;
        }
        // no data? wait it
        else if (!real && !wait)
        {
            // wait it
            wait = tb_socket_wait(sock, TB_SOCKET_EVENT_SEND, TB_DEMO_TIMEOUT);
            tb_assert_and_check_break(wait >= 0);
        }
        // failed or end?
        else break;
    }

    // ok?
    return send == size;
}
static tb_char_t const* tb_demo_http_session_code_cstr(tb_size_t code)
{
    // done
    tb_char_t const* cstr = tb_null;
    switch (code)
    {
    case TB_HTTP_CODE_OK:                       cstr = "OK"; break;
    case TB_HTTP_CODE_BAD_REQUEST:              cstr = "Bad Request"; break;
    case TB_HTTP_CODE_NOT_FOUND:                cstr = "Not Found"; break;
    case TB_HTTP_CODE_NOT_IMPLEMENTED:          cstr = "Not Implemented"; break;
    case TB_HTTP_CODE_NOT_MODIFIED:             cstr = "Not Modified"; break;
    case TB_HTTP_CODE_INTERNAL_SERVER_ERROR:    cstr = "Internal Server Error"; break;
    default: break;
    }

    // check
    tb_assert(cstr);

    // ok?
    return cstr;
}
static tb_bool_t tb_demo_http_session_resp_send(tb_demo_http_session_ref_t session)
{
    // check
    tb_assert_and_check_return_val(session && session->sock, tb_false);

    // make the response header
    tb_long_t size = tb_snprintf(   (tb_char_t*)session->data
                                ,   sizeof(session->data) -1
                                ,   "HTTP/1.1 %lu %s\r\n"
                                    "Server: %s\r\n"
                                    "Content-Type: text/html\r\n"
                                    "Content-Length: %llu\r\n"
                                    "Connection: %s\r\n"
                                    "\r\n"
                                ,   session->code
                                ,   tb_demo_http_session_code_cstr(session->code)
                                ,   TB_VERSION_SHORT_STRING
                                ,   session->file? tb_file_size(session->file) : 0
                                ,   session->keep_alive? "keep-alive" : "close");
    tb_assert_and_check_return_val(size > 0, tb_false);

    // end
    session->data[size] = 0;

    // send the response header
    if (!tb_demo_http_session_data_send(session->sock, session->data, size)) return tb_false;

    // send the response file if exists
    if (session->file && !tb_demo_http_session_file_send(session->sock, session->file)) return tb_false;

    // ok
    return tb_true;
}
static tb_void_t tb_demo_http_session_head_parse(tb_demo_http_session_ref_t session)
{
    // check
    tb_assert_and_check_return(session);

    // the first line? 
    tb_char_t const* p = session->line;
    if (!session->line_index)
    {
        // parse get
        if (!tb_strnicmp(p, "GET", 3))
        {
            session->code       = TB_HTTP_CODE_OK;
            session->method     = TB_HTTP_METHOD_GET;
            p += 3;
        }
        // parse post
        else if (!tb_strnicmp(p, "POST", 4))
        {
            session->code       = TB_HTTP_CODE_NOT_IMPLEMENTED;
            session->method     = TB_HTTP_METHOD_POST;
            p += 4;
        }
        // other method is not implemented
        else session->code = TB_HTTP_CODE_NOT_IMPLEMENTED;

        // get or post? parse the path
        if (    session->method == TB_HTTP_METHOD_GET
            ||  session->method == TB_HTTP_METHOD_POST)
        {
            // skip space
            while (*p && tb_isspace(*p)) p++;

            // append path
            tb_size_t i = 0;
            while (*p && !tb_isspace(*p) && i < sizeof(session->path) - 1) session->path[i++] = *p++;
            session->path[i] = '\0';
        }
    }
    // key: value?
    else
    {
        // seek to value
        while (*p && *p != ':') p++;
        tb_assert_and_check_return(*p);
        p++; while (*p && tb_isspace(*p)) p++;

        // no value
        tb_check_return(*p);

        // parse content-length
        if (!tb_strnicmp(session->line, "Content-Length", 14))
            session->content_size = tb_stou64(p);
        // parse connection
        else if (!tb_strnicmp(session->line, "Connection", 10))
            session->keep_alive = !tb_stricmp(p, "keep-alive");
        // parse range
        else if (!tb_strnicmp(session->line, "Range", 5))
            session->code = TB_HTTP_CODE_NOT_IMPLEMENTED;
    }
}
static tb_long_t tb_demo_http_session_head_line(tb_demo_http_session_ref_t session, tb_byte_t const* data, tb_size_t size)
{
    // check
    tb_assert_and_check_return_val(session && data && size, -1);

    // done
    tb_long_t ok = 0;
    do
    {
        // done
        tb_char_t           ch = '\0';
        tb_char_t const*    p = (tb_char_t const*)data;
        tb_char_t const*    e = p + size;
        while (p < e)
        {
            // the char
            ch = *p++;

            // error end?
            if (!ch)
            {
                ok = -1;
                break;
            }

            // append char to line
            if (ch != '\n') session->line[session->line_size++] = ch;
            // is line end?
            else
            {
                // line end? strip '\r\n'
                if (session->line_size && session->line[session->line_size - 1] == '\r') 
                {
                    session->line_size--;
                    session->line[session->line_size] = '\0';
                }

                // trace
                tb_trace_d("head: %s", session->line);
    
                // end?
                if (!session->line_size) 
                {
                    // ok
                    ok = 1;
                    break;
                }

                // parse the header line
                tb_demo_http_session_head_parse(session);

                // clear line 
                session->line_size = 0;

                // update line index
                session->line_index++;
            }
        }

    } while (0);

    // ok?
    return ok;
}
static tb_bool_t tb_demo_http_session_head_recv(tb_demo_http_session_ref_t session)
{
    // check
    tb_assert_and_check_return_val(session && session->sock, tb_false);

    // read data
    tb_long_t wait = 0;
    tb_long_t ok = 0;
    while (!ok)
    {
        // read it
        tb_long_t real = tb_socket_recv(session->sock, session->data, sizeof(session->data));

        // has data?
        if (real > 0) 
        {
            // get the header line
            ok = tb_demo_http_session_head_line(session, session->data, real);

            // clear wait events
            wait = 0;
        }
        // no data? wait it
        else if (!real && !wait)
        {
            // wait it
            wait = tb_socket_wait(session->sock, TB_SOCKET_EVENT_RECV, TB_DEMO_TIMEOUT);
            tb_assert_and_check_break(wait >= 0);
        }
        // failed or end?
        else break;
    }

    // ok?
    return ok > 0;
}
static tb_void_t tb_demo_coroutine_client(tb_cpointer_t priv)
{
    // check
    tb_socket_ref_t sock = (tb_socket_ref_t)priv;
    tb_assert_and_check_return(sock);

    // done
    tb_demo_http_session_t session;
    do
    {
        // init session
        if (!tb_demo_http_session_init(&session, sock)) break;

        // read the request header
        if (!tb_demo_http_session_head_recv(&session)) break;

        // trace
        tb_trace_d("path: %s", session.path);

        // get file?
        if (session.method == TB_HTTP_METHOD_GET)
        {
            // make full path
            tb_long_t size = tb_snprintf((tb_char_t*)session.data, sizeof(session.data) - 1, "%s%s%s", g_roordir, session.path[0] != '/'? "/" : "", session.path);
            if (size > 0) session.data[size] = 0;

            // init file
            session.file = tb_file_init((tb_char_t*)session.data, TB_FILE_MODE_RO | TB_FILE_MODE_BINARY);

            // not found?
            if (!session.file) session.code = TB_HTTP_CODE_NOT_FOUND;
        }

        // send the response 
        if (!tb_demo_http_session_resp_send(&session)) break;

    } while (0);

    // exit session
    tb_demo_http_session_exit(&session);
}
static tb_void_t tb_demo_coroutine_listen(tb_cpointer_t priv)
{
    // done
    tb_socket_ref_t sock = tb_null;
    do
    {
        // init socket
        sock = tb_socket_init(TB_SOCKET_TYPE_TCP, TB_IPADDR_FAMILY_IPV4);
        tb_assert_and_check_break(sock);

        // bind socket
        tb_ipaddr_t addr;
        tb_ipaddr_set(&addr, tb_null, TB_DEMO_PORT, TB_IPADDR_FAMILY_IPV4);
        if (!tb_socket_bind(sock, &addr)) break;

        // listen socket
        if (!tb_socket_listen(sock, 1000)) break;

        // trace
        tb_trace_i("listening %lu ..", TB_DEMO_PORT);

        // wait accept events
        while (tb_socket_wait(sock, TB_SOCKET_EVENT_ACPT, -1) > 0)
        {
            // accept client sockets
            tb_size_t       count = 0;
            tb_socket_ref_t client = tb_null;
            while ((client = tb_socket_accept(sock, tb_null)))
            {
                // start client connection
                if (!tb_coroutine_start(tb_null, tb_demo_coroutine_client, client, 0)) break;
                count++;
            }

            // trace
            tb_trace_d("listened %lu", count);
        }

    } while (0);

    // exit socket
    if (sock) tb_socket_exit(sock);
    sock = tb_null;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * main
 */ 
tb_int_t tb_demo_coroutine_http_server_main(tb_int_t argc, tb_char_t** argv)
{
    // init the root directory
    if (argv[1]) tb_strlcpy(g_roordir, argv[1], sizeof(g_roordir));
    else tb_directory_current(g_roordir, sizeof(g_roordir));

    // trace
    tb_trace_i("rootdir: %s", g_roordir);

    // init scheduler
    tb_scheduler_ref_t scheduler = tb_scheduler_init();
    if (scheduler)
    {
        // start coroutines
        tb_coroutine_start(scheduler, tb_demo_coroutine_listen, tb_null, 0);

        // run scheduler
        tb_scheduler_loop(scheduler);

        // exit scheduler
        tb_scheduler_exit(scheduler);
    }
    return 0;
}
