#include "mongoose.h"
#include "json.hpp"
#include <cstring>
#include <iostream>
#include <deque>
#include <string>

#include "json.hpp"

using json = nlohmann::json;
using namespace std;

static const char *s_http_port = "8000";
bool hasPopped;
deque<string> songQueue;

static void ev_handler(struct mg_connection *c, int ev, void *p) {
    if (ev == MG_EV_HTTP_REQUEST) {
        struct mg_serve_http_opts opts;
        struct http_message *hm = (struct http_message *) p;

        // -------------------- / -------------------- //
        if(mg_vcmp(&hm->method, "GET") == 0){
            if(mg_vcmp(&hm->uri, "/") == 0){
                mg_http_serve_file(c, hm, "app.html",
                          mg_mk_str("text/html"), mg_mk_str(""));
            }
        }

        // -------------------- /add/<track_id> -------------------- //
        if(mg_vcmp(&hm->method, "GET") == 0){
            if(mg_strncmp(hm->uri, mg_mk_str("/add/"), 5) == 0) {
                // BEGIN SHANE CODE
                const char * fuckery = hm->uri.p + 5;
                string track;
                while(*fuckery != ' ') track += *fuckery++; 
                songQueue.push_back(track);
                json songs(songQueue);
                // END SHANE CODE;   
                mg_send_head(c, 200, -1, "");
                mg_send_http_chunk(c, songs.dump().c_str(), songs.dump().length());
                mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                cout << songs << endl;
            }
        }

        // -------------------- /pop -------------------- //
        if(mg_vcmp(&hm->method, "POST") == 0){
            if(mg_vcmp(&hm->uri, "/pop") == 0){
                if(songQueue.size() != 0){
                    if(hasPopped) songQueue.pop_front();
                    else hasPopped = true;
                    mg_send_head(c, 200, -1, "");
                    mg_send_http_chunk(c, songQueue.front().c_str(), songQueue.front().length());
                    mg_send_http_chunk(c, "", 0); // Tell the client we're finished                
                }
                else{
                    mg_send_head(c, 200, -1, "");
                    mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                }
            }
        }

        // -------------------- /queue -------------------- //
        if(mg_vcmp(&hm->method, "GET") == 0){
            if(mg_vcmp(&hm->uri, "/queue") == 0){
                json songs(songQueue);
                mg_send_head(c, 200, -1, "");
                mg_send_http_chunk(c, songs.dump().c_str(), songs.dump().length());
                mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                cout << songs << endl;
            }
        }
    }
}

int main(void){
    struct mg_mgr mgr;
    struct mg_connection *c;
    hasPopped = false;

    mg_mgr_init(&mgr, NULL);
    c = mg_bind(&mgr, s_http_port, ev_handler);
    mg_set_protocol_http_websocket(c);

    for(;;){
        // mg_mgr_poll() iterates over all sockets, accepts new connections,
        // sends and receives data, closes connections and calls event handler 
        // functions for the respective events
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);

    return 0;
}