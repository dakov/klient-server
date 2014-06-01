#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h> // gethostbyname
#include <errno.h>


#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include <signal.h>
#include <sys/wait.h>

#define PROTOCOL "GETME"
#define BUFF_SIZE 1024

#define RESP_BUFF_SIZE 11
using namespace std;

enum TException {
    E_OK, E_ARG, E_DNS, E_SOCKET, E_CONNECT, E_SEND, E_READ, E_RESP, E_FILE, E_FILE_RECV
};

/**
 * Konfiguracni "struktura"
 */
class Cfg {
public:

    string host, file;
    int port;

    Cfg(string host, int port, string file) {
	this->host = host;
	this->file = file;
	this->port = port;
    }

};

Cfg argparse(int argc, char ** argv) {

    if (argc != 2)
	throw E_ARG;

    //ocekavany format: host:port/soubor

    string arg = argv[1];

    int delim1 = arg.find(':');
    int delim2 = arg.find('/');

    string host = arg.substr(0, delim1);
    string port = arg.substr(delim1 + 1, delim2 - (delim1 + 1));
    string file = arg.substr(delim2 + 1);

    if (host.empty() || port.empty() || file.empty()) {
	throw E_ARG;
    }

    return Cfg(host, atoi(port.c_str()), file);
}

/**
 * Pokusi se vytvorit a pripojit socket klienta. 
 * 
 * @param cfg Ridici konfiguracni struktura
 * @return Popisovac nove otevreneho socketu klienta
 */
int sconnect(Cfg cfg) {

    int outSocket;
    struct sockaddr_in sin;

    // preklad domenoveho jmena na IP adresu
    struct hostent *he = gethostbyname(cfg.host.c_str());

    if (he == NULL) {
	throw E_DNS;
    }

    /* Vytvoreni socketu klienta */
    if ((outSocket = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
	throw E_SOCKET;
    }

    /* Naplneni struktury sockaddr_in */
    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof (server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(cfg.port);

    memcpy(&server_address.sin_addr, he->h_addr_list[0], he->h_length);

    /* Pokus o pripojeni socketu */
    if (connect(outSocket, (struct sockaddr *) &server_address, sizeof (server_address)) < 0) {
	throw E_CONNECT;
    }

    /* Vraci popisovac socketu */
    return outSocket;
}

/**
 * Zasle serveru zpravu v protokolu GETME, ve ktere zada o zaslani souboru
 * 
 * @param socket Socket, pres kterz je pozadavek zaslan
 * @param filename Jmeno souboru, ktere je pozadovano
 */
void askForFile(int socket, string filename) {

    string msg = string(PROTOCOL) + " " + filename + "\r\n";

    /* zaslani pozadavku  */
    if (write(socket, msg.c_str(), msg.length()) < 0) {
	throw E_SEND;
    }

}

/**
 * Prected odpoved serveru na zadost o zaslani souboru. Dodrzuje konvence
 * protokolu GETME. Tedy delka odpovedi je maximalne 10B, ve tvaru: GETME XY\r\n
 * je X a Y je dvojice cislic udavajici uspesnost akce / pricinu chyby
 * 
 * @param outSocket Socket, na ktereme cekame odpoved
 * @return Retezec s plnym znenim odpovedi
 */
string getResponse(int socket) {

    string res = "";

    char buff[RESP_BUFF_SIZE];
    int bytes;

    memset(buff, 0, RESP_BUFF_SIZE);
    bytes = read(socket, buff, RESP_BUFF_SIZE - 1);

    if (bytes < 0)
	throw (E_READ);

    res.append(buff);

    return res;
}

int getReturnCode(string msg) {
    // odpoved je definovana tak, ze zacina "GETME " nasleduje kod odpovedi
    msg = msg.substr(6);

    return (atoi(msg.c_str()));
}

/**
 * Na zadanem soketu prijme posilany soubor
 * @param socket
 * @param filename
 */
void receiveFile(int socket, string filename) {

    FILE *f = NULL;
    char buff[BUFF_SIZE];

    if ((f = fopen(filename.c_str(), "w")) == NULL) {
	throw E_FILE;
    }

    int bytes;
    string res = "";

    cerr << "[CLIENT] Transmission of '" << filename << "' started" << endl;

    /*
    while (!feof(socket_data)) {
	bzero(buff, BUFF_SIZE);
	int bytes = fread(buff, sizeof (char), BUFF_SIZE, socket_data);
	fwrite(buff, sizeof (char), bytes, f);
    } */
    
    while ( true ) {
	
	bytes = read(socket, buff, BUFF_SIZE);
	
	if ( bytes <= 0 ){
	    break;
	}
	    
	fwrite(buff, sizeof (char), bytes, f);
	//cerr << "written " << bytes << " bytes" <<endl;
    }
    

    /*do {
       int bytes = (read(socket, buff, BUFF_SIZE));
      
       fwrite(buff, sizeof(char), bytes, f);
      
       if (bytes < BUFF_SIZE) 
	   break;
      
     } while (true); */


    if (fclose(f) == EOF) {
	cerr << "[INFO] Soubor se nepodarilo zavrit" << endl;
    } else {
	cerr << "[CLIENT] Transmission of '" << filename << "' finished" << endl;
    }

}

int main(int argc, char** argv) {

    int outSocket;

    Cfg cfg = Cfg("", 0, "");

    try {
	cfg = argparse(argc, argv);
    } catch (TException ex) {
	cerr << "[ CLIENT ] Neplatny format / pocet argumentu" << endl;
	return ex;
    }

    try {
	/* Pripoj socket */
	outSocket = sconnect(cfg);
	askForFile(outSocket, cfg.file);

	string response = getResponse(outSocket);

	int code = getReturnCode(response);

	if (code != 0) {
	    throw E_RESP;
	}

	receiveFile(outSocket, cfg.file);

    } catch (TException ex) {
	string msg;

	switch (ex) {
	    case E_DNS: msg = "Nepodarilo se prelozit domenove jmeno.";
		break;
	    case E_SOCKET: msg = "Nepodarilo se vytvorit socket.";
		break;
	    case E_CONNECT: msg = "Nepodarilo se pripojit ( connect() ).";
		break;
	    case E_SEND: msg = "Nepodarilo se odeslat zpravu.";
		break;
	    case E_READ: msg = "Chyba pri cteni socketu.";
		break;
	    case E_RESP: msg = "Negativni odpoved serveru.";
		break;
	    case E_FILE: msg = "Nepodarilo cilovy soubor";
		break;
	    case E_FILE_RECV: msg = "Chyba pri prijimani souboru";
		break;
	}

	//close(outSocket);

	cerr << "[ CLIENT: ]" << msg << endl;
	return ex;
    }

    if (close(outSocket) != 0){
	cerr << "[INFO] Nepodarilo se uzavrit komunikacni socket\n";
	cerr << strerror(errno);
    } 




    return 0;
}

