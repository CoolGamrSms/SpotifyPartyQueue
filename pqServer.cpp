#include "mongoose.h"
#include "json.hpp"
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <openssl/hmac.h>
#include <openssl/sha.h>

using json = nlohmann::json;
using namespace std;

static const char *s_http_port = "8000";

namespace ns {
    struct queueEntry {
        string song;
        string addedBy;
    };

    void to_json(json& j, const queueEntry& p) {
        j = json{{"song", p.song}, {"addedBy", p.addedBy}};
    }
} // namespace ns

vector<ns::queueEntry> songQueue;
unsigned int currentTrack, queueLength, latestHostPos;
string password, serverName;
bool passwordIsSet;

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

        // -------------------- /public -------------------- //
        if(mg_vcmp(&hm->method, "GET") == 0){
            if(mg_vcmp(&hm->uri, "/public") == 0){
            }
        }

        // -------------------- /add/<track_id> -------------------- //
        if(mg_vcmp(&hm->method, "POST") == 0){
            if(mg_strncmp(hm->uri, mg_mk_str("/add/"), 5) == 0) {
                if(queueLength >= 20) mg_send_head(c, 403, -1, "");
                else{
                    queueLength++;
                    ns::queueEntry qentry;
                    const char * track_id = hm->uri.p + 5;
                    string track, hmac;
                    while(*track_id != ' ') track += *track_id++; 
                    qentry.song = track;

                    if(hm->body.len != 0){
                        const char * data = hm->body.p;
                        char *name, *hash;
                        if((name = strstr(data, "name:")) != NULL) {
                            name += 6;
                            while(*name != '\n') qentry.addedBy += *name++; 
                        }
                        else qentry.addedBy = "";
                        if((hash = strstr(data, "hmac:")) != NULL) {
                            hash += 6;
                            while(*hash != '\n') hmac += *hash++; 
                        }
                    }
                    else qentry.addedBy = "";

                    unsigned char* digest;    
                    string arg = qentry.song + qentry.addedBy;

                    digest = HMAC(EVP_sha256(), password.c_str(), strlen(password.c_str()), (unsigned char*)arg.c_str(), strlen(arg.c_str()), NULL, NULL);
                    if((passwordIsSet && hmac.compare((char*)digest) == 0) || !passwordIsSet){
                        songQueue.push_back(qentry);
                        json songs(songQueue); 
                        mg_send_head(c, 200, -1, "");
                        mg_send_http_chunk(c, songs.dump().c_str(), songs.dump().length());
                        mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                        cout << songs << endl;
                    }
                    else{
                        mg_send_head(c, 401, -1, "");
                    }
                }
            }
        }

        // -------------------- /update_queue -------------------- //
        if(mg_vcmp(&hm->method, "POST") == 0){
            if(mg_vcmp(&hm->uri, "/update_queue") == 0){
                cout << "Received host update_queue request.\n";
                if(latestHostPos + 1 < songQueue.size()){
                    string newSongs;
                    latestHostPos++;
                    while(latestHostPos + 1 < songQueue.size()){
                        newSongs.append(songQueue[latestHostPos].song);
                        newSongs.append("\n");
                        latestHostPos++;
                    }
                    mg_send_head(c, 200, -1, "");
                    mg_send_http_chunk(c, newSongs.c_str(), newSongs.length());
                    mg_send_http_chunk(c, "", 0); // Tell the client we're finished    
                }
                else{
                    cout << "No new tracks." << endl;
                    mg_send_head(c, 200, -1, "");
                    mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                }
            }
        }

        /*
        // -------------------- /pop -------------------- //
        if(mg_vcmp(&hm->method, "POST") == 0){
            if(mg_vcmp(&hm->uri, "/pop") == 0){
                cout << "Received pop request.\nCurrent track number: " << currentTrack << endl;
                if(currentTrack + 1 < songQueue.size()){
                    currentTrack++;
                    cout << "New track number: " << currentTrack << endl;
                    mg_send_head(c, 200, -1, "");
                    mg_send_http_chunk(c, songQueue[currentTrack].song.c_str(), songQueue[currentTrack].song.length());
                    mg_send_http_chunk(c, "", 0); // Tell the client we're finished    
                }
                else{
                    cout << "New track number: " << currentTrack << endl;
                    mg_send_head(c, 200, -1, "");
                    mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                }
            }
        }
        */

        // -------------------- /queue -------------------- //
        if(mg_vcmp(&hm->method, "GET") == 0){
            if(mg_vcmp(&hm->uri, "/queue") == 0){
                json songs;
                for(int i = currentTrack; i < songQueue.size(); ++i) songs.push_back(songQueue[i]);
                mg_send_head(c, 200, -1, "");
                mg_send_http_chunk(c, songs.dump().c_str(), songs.dump().length());
                mg_send_http_chunk(c, "", 0); // Tell the client we're finished
                cout << songs << endl;
            }
        }

        // -------------------- /name -------------------- //
        if(mg_vcmp(&hm->method, "GET") == 0){
            if(mg_vcmp(&hm->uri, "/name") == 0){
                mg_send_head(c, 200, -1, "");
                mg_send_http_chunk(c, serverName.c_str(), serverName.length());
                mg_send_http_chunk(c, "", 0); // Tell the client we're finished
            }
        }

        // -------------------- /update_now_playing/<action> -------------------- //
        if(mg_vcmp(&hm->method, "POST") == 0){
            if(mg_vcmp(&hm->uri, "/update_now_playing/") == 0){
                const char * uriAction = hm->uri.p + 20;
                string action;
                while(*uriAction != ' ') action += *uriAction++; 

                if(action.compare("back") == 0){
                    if(currentTrack > 0){
                        currentTrack--;
                        queueLength++;
                    }
                }
                else if(action.compare("forward") == 0){
                    if(currentTrack + 1 < songQueue.size()){
                        currentTrack++;
                        queueLength--;
                    }
                }

                mg_send_head(c, 200, -1, "");
                mg_send_http_chunk(c, "", 0); // Tell the client we're finished
            }
        }        
    }
}

int main(int argc, char *argv[]){
    struct mg_mgr mgr;
    struct mg_connection *c;
    currentTrack = 0;
    queueLength = 0;
    latestHostPos = 0;
    passwordIsSet = false;
    if(argc < 2){
        cout << "Usage: ./pqServer [serverName] [optional: password]\n";
        return 1;
    }
    serverName = argv[1];
    if(argc == 3){
        password = argv[2];
        passwordIsSet = true;
    }

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