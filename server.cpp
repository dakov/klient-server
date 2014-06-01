#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h> // gethostbyname
#include <errno.h>


#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
 
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>

#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <errno.h>

#define BUFF_SIZE 1024
#define GETME_FILE_NOT_ACCESSIBLE "10"
#define GETME_OK "00"

using namespace std;


enum TException {
    E_OK, E_ARG, E_SOCKET, E_BIND, E_LISTEN, E_ACCEPT, E_READ, E_FILE,
    E_REPLY, E_FILE_SEND, E_FORK
};

class Cfg {
    
public:
    static const int QUEUE_MAX;
    
    int port, limit;

    Cfg(int port, int limit) {
	this->port = port;
	this->limit = limit;
    }

};

const int Cfg::QUEUE_MAX = 50;


/**
 * Zpracuje a vyhodnoti argumenty
 * @param argc Pocet argumentu
 * @param argv Argumenty
 * @return Ridici strukturu
 */
Cfg argParse(int argc, char ** argv) {
    
    string port = "";
    string limit = "";
	
    if (argc != 5){
	throw E_ARG;
    }

    for ( int i = 0; i < argc; ) {
	string arg = argv[i];
	
	if (arg == "-p" && (i % 2)  == 1) {
	    
	    port = argv[i+1];
	    i += 2;
	    continue;
	}
	else if (arg == "-d" && (i % 2) == 1) {
	    
	    limit = argv[i+1];
	    i += 2;
	    continue;
	}
	
	++i;
    }
    
    if ( port == "" || limit == ""){
	throw E_ARG;
    }
    
    int nport = atoi( port.c_str() );
    int nlimit = atoi( limit.c_str() );
    
    return Cfg(nport, nlimit);
}


void finalizeProcess(int status)
{
  fprintf(stderr, "[SERVER] Child process finalized.\n");
  wait(&status);
}


/**
 * Posle odpoved klientovi. Vyhazuje vyjimku E_REPLY
 * @param socket Komunikacni soket
 * @param msg Obsah zpravy
 */
void reply(int socket, string msg) {
    
    if ( write(socket, msg.c_str(), msg.length()) < 0 )
    {  
	throw E_REPLY;
    }
    
}

/**
 * Zpracuje pozadavek na zaslani souboru
 * @param socket Soket, kde ocekava zpravu
 * @return Jmeno pozadovaneho souboru
 */
string process_askForFile(int socket) {
    
    string filename = "";
    string res = "";

    char buff[BUFF_SIZE];
    int bytes;
    
    do {
	memset(buff, 0, BUFF_SIZE);
	
        bytes = read(socket, buff, BUFF_SIZE);
	
        if (bytes == 0)
            break;
        else if( bytes < 0)
            throw(E_READ);
        
        buff[bytes] = '\0';
        
        res.append(buff);
	
    } while ( buff[BUFF_SIZE - 1] != 0 );
    
    /*
     * Protokol obsahuje pouze jediny pozadavek, navic je garantovano, ze zprava
     * zacina retezcem "GETME" nasledovanym mezerou, za kterou se nachazi jmeno
     * pozadovaneho souboru.
     */
    
    filename = res.substr(6, res.length()-(6+2)); // 6 -> 'README '; +2 - na konci je \r\n

    if (filename.empty() )
	throw E_FILE;
    
    return filename;
}

/**
 * Odpovi klientovi na zadost zaslani souboru (OK - poslu | FAIL - neposlu) a 
 * otevre pozadovany soubor
 * @param commSocket Soket na ktery se posila odpoved
 * @param filename Jmeno pozadovaneho souboru
 * @return Deskriptor otevreneho souboru
 */
FILE * answer_askForFile(int commSocket, string filename) {
    
    char buff[BUFF_SIZE] = {0};
    FILE *f = NULL;
    string response = "GETME ";
    
    if((f = fopen(filename.c_str(), "r")) == NULL) 
    {
	// chyba pri otevreni souboru
	response +=  GETME_FILE_NOT_ACCESSIBLE;
	response += "\r\n";
    }
    else 
    {
	response += GETME_OK;
	response += "\r\n";
	
    }
    
    reply(commSocket, response); //muze vyhodi vyjimku -> je poslana vys
    
    return f;
}

/**
 * Posle davku souboru. Davka odpovida sade dat, ktera server posle zadanou 
 * rychlosti za jednu vterinu.
 * @param commSocket Soket pro posilani dat
 * @param f Zdrojovy soubor
 * @param kbps Omezeni rychlost
 * @return Pocet prectenych bytu
 */
int sendBatch(int commSocket, FILE * f, int kbps) {
    
    char buff[BUFF_SIZE];
    
    int readBytes;
    
    int i;
    
    
    for (i = 0; i < kbps; i++) {
	readBytes = fread(buff, 1, BUFF_SIZE, f); 

	if (readBytes > 0) {
	    if(write(commSocket, buff, readBytes) < 0)  
	    {
	      cerr << "chyba: " << strerror(errno) << endl;
	      fprintf(stderr, "ERROR: zapis souboru do soketu\n");
	      throw E_FILE_SEND;
	    }
	}
	
	if (readBytes < BUFF_SIZE) {
	    readBytes = 0;
	    break;
	}
	
    }
    
    return i*readBytes;    
    
}

/**
 * Zasle cely soubor zadanou rychlosti
 */
void m_sendfile(int commSocket, FILE * f, int kbps) {
    
    while ( true ) {
	
	clock_t start, end;
	double delta;
	
	start = clock();
	
	
	int x = sendBatch(commSocket, f, kbps); // posle davku za 1s
	
	if ( x == 0 ) {
	    break;
	}
	
	end = clock();
	
	delta = (end-start)/(double)CLOCKS_PER_SEC;
	
	int sec = 1000000;
	
	if( delta > sec )
	    continue;
	
	int sleepfor = sec-(delta*sec);
	
	
	if(sleepfor > 0){
	    usleep(sleepfor);
	}
    }
    
}

/**
 * Inicializuje kontrolni spojeni, na kterem server posloucha a zpracovava
 * pozadavky klientu
 * 
 * @param cfg Ridici konfiguracni struktura
 */
void initializeConnection(Cfg cfg) { 
    
        int inSocket;
    
    struct sockaddr_in sin;
    
    sin.sin_family = PF_INET;              
    sin.sin_port = htons(cfg.port);  
    sin.sin_addr.s_addr  = INADDR_ANY;
    
    
    signal(SIGCHLD, finalizeProcess);
    
    string msg_recv;
    
    /* Vytvoreni socketu */
    if( (inSocket = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
    {
	throw E_SOCKET;
    }
    
    /* Pripojeni socketu */
    if (bind(inSocket, (struct sockaddr *)&sin, sizeof(sin) ) < 0 )
    {
	cerr << strerror(errno) << endl;
	throw E_BIND;
    }
    
    /* Socket bude poslouchat a cekat na prichozi pozadavky */
    if((listen(inSocket, cfg.QUEUE_MAX)) != 0)//Vytvorim frontu pro prichozi pozadavky
    {
        throw E_LISTEN;
    }
    
    socklen_t sinlen = sizeof(sin);
    
    while (true) 
    {
	int commSocket;
	
	/* Blokujici operace cekajici na pripojeni klienta */
	/* Vraci deskriptor komunikacniho socketu */
	if( (commSocket = accept(inSocket, (struct sockaddr *)&sin, &sinlen) ) < 0 )
	{
	  throw E_ACCEPT;
	}
	
	/* Prisel pozadavek od klienta => odblokuje se postup v cyklu */
	
	/* Pro nove klienta vytvor samostatny proces, ktery bude posilat data */
	int pid = fork(); 
	
	if ( pid < 0)
	{ // nepodaril se fork
	    throw E_FORK;
	}
	else if (pid == 0) /* Potomek => posilaci proces */
	{
	    try {
		// komunikaci zacal klient - zjisti, jaky chce soubor
		string filename = process_askForFile(commSocket);
		cerr << "[ SERVER ] Some client asked for " << filename << "\n"; 
		cerr.flush();
		
		// vyjimka ve filename zajisti, ze zde bude jmeno souboru dostupne
		
		FILE *file = answer_askForFile(commSocket, filename);
		
		if ( file == NULL ) {
		    throw E_FILE;
		}
		
		m_sendfile(commSocket, file, cfg.limit);
		
		fclose(file);
		
	    } 
	    catch (TException ex)
	    {
		/* Nastala vyjimka, zavri komunikacni soket a posli vyjimku vys */
		/* POZOR: obsluha vyjimky vyse nesmi zavirat zadne sdilene prostredky! */
		close(commSocket);
		cerr << "[ SERVER ] Some client finished unsuccessfully\n";
		throw ex;
	    }
	    
	    
	    
	    //close(commSocket);
	   // if (close(commSocket) != 0) {
	    if (shutdown(commSocket, SHUT_WR) != 0) {
		cerr << "Uzavreni socketu se nezdarilo" << endl;
	    }
	    
	    cerr << "[ SERVER ] Some client finished successfully\n";
	    exit(EXIT_SUCCESS);
	}
	
	/* Zde pokracuje rodic - hlavni proces - ten nic nedela, jen zase cekat */
	
    } // while true
    
    // zavreni socketu - pomuze to, kdyz nejde cyklus bezpecne ukoncit?
    close(inSocket);
    cerr << "[ SERVER ] Finished\n"; // prenos zahajen
    
}



int main(int argc, char** argv) {
    
    Cfg cfg = Cfg(0,0); //pouze implicitní hodnota kvuli prekladu

    try
    {
	cfg = argParse(argc, argv);
    } 
    catch (TException ex) 
    {
	cerr << "Neplatny pocet nebo format argumentu" << endl;
	return ex;
    }
    
    
    try
    {
	initializeConnection( cfg );
    }
    catch (TException ex)
    {
	string msg;
	
	switch (ex) {
	    case E_SOCKET: msg = "Nepodarilo se vytvorit socket."; break;
	    case E_BIND: msg = "Nepodaril se bind socketu a IP adresy;"; break;
	    case E_LISTEN: msg = "Nepodaril se listen socketu"; break;
	    case E_ACCEPT: msg = "Nepodaril se accept()"; break;
	    case E_READ: msg = "Chyba pri cteni socketu"; break;
	    case E_FILE: msg = "Chyba pri cteni souboru"; break;
	    case E_REPLY: msg = "Chyba pri posilani odpovedi"; break;
	    case E_FILE_SEND: msg = "Chyba pri posilani souboru"; break;
	    case E_FORK: msg = "Forking child process failed"; break;
	}
	
	cerr << "[ SERVER ] " << msg << endl;
	
	return ex;
    }
	
    
    
    
    return 0;
}

