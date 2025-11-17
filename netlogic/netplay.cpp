
/* This contains the network play functions and data */

#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

#include "Maelstrom_Globals.h"
#include "netplay.h"
#include "protocol.h"


int   gNumPlayers;
int   gOurPlayer;
int   gDeathMatch;
NET_DatagramSocket *gSocket;

static int            GotPlayer[MAX_PLAYERS];
static NET_Address   *PlayAddr[MAX_PLAYERS];
static Uint16         PlayPort[MAX_PLAYERS];
static NET_Address   *ServAddr;
static Uint16         ServPort;
static int            FoundUs, UseServer;
static Uint32         NextFrame;
NET_Datagram         *OutBound[2];
static int            CurrOut;
/* This is the data offset of a SYNC packet */
#define PDATA_OFFSET	(1+1+sizeof(Uint32)+sizeof(Uint32))

/* We keep one packet backlogged for retransmission */
#define OutBuf		OutBound[CurrOut]->buf
#define OutLen		OutBound[CurrOut]->buflen
#define LastBuf		OutBound[!CurrOut]->buf
#define LastLen		OutBound[!CurrOut]->buflen

static unsigned char *SyncPtrs[2][MAX_PLAYERS];
static unsigned char  SyncBufs[2][MAX_PLAYERS][BUFSIZ];
static int            SyncLens[2][MAX_PLAYERS];
static int            ThisSyncs[2];
static int            CurrIn;

/* We cache one packet if the other player is ahead of us */
#define SyncPtr		SyncPtrs[CurrIn]
#define SyncBuf		SyncBufs[CurrIn]
#define SyncLen		SyncLens[CurrIn]
#define ThisSync	ThisSyncs[CurrIn]
#define NextPtr		SyncPtrs[!CurrIn]
#define NextBuf		SyncBufs[!CurrIn]
#define NextLen		SyncLens[!CurrIn]
#define NextSync	ThisSyncs[!CurrIn]

#define TOGGLE(var)	var = !var

static NET_Datagram *NET_AllocPacket(size_t size)
{
	NET_Datagram* packet = (NET_Datagram*)SDL_calloc(1, sizeof(*packet) + size);
	if (packet) {
		packet->buf = (Uint8*)(packet + 1);
		packet->buflen = (int)size;
	}
	return packet;
}

SDL_FORCE_INLINE void NET_Write16(Uint16 value, void* areap)
{
	*(Uint16*)areap = SDL_Swap16BE(value);
}

SDL_FORCE_INLINE void NET_Write32(Uint32 value, void* areap)
{
	*(Uint32*)areap = SDL_Swap32BE(value);
}

SDL_FORCE_INLINE Uint16 NET_Read16(const void* areap)
{
	return SDL_Swap16BE(*(const Uint16*)areap);
}

SDL_FORCE_INLINE Uint32 NET_Read32(const void* areap)
{
	return SDL_Swap32BE(*(const Uint32*)areap);
}

static int GetPlayerFromPacket(NET_Datagram *packet)
{
	for ( int player = 0; player < MAX_PLAYERS; ++player ) {
		if ( !PlayAddr[player] ) {
			continue;
		}
		if ( NET_CompareAddresses(packet->addr, PlayAddr[player]) == 0 &&
			packet->port == PlayPort[player] ) {
			return player;
		}
	}
	return -1;
}

static bool SendPlayer(int player, const void* buf, int buflen)
{
	return NET_SendDatagram(gSocket, PlayAddr[player], PlayPort[player], buf, buflen);
}

static void SendAllPlayers(const void *buf, int buflen)
{
	for ( int player = 0; player < MAX_PLAYERS; ++player )
	{
		if ( !PlayAddr[player] ) {
			continue;
		}
		SendPlayer(player, buf, buflen);
	}
}

int InitNetData(void)
{
	int i;

	/* Initialize the networking subsystem */
	if ( !NET_Init() ) {
		error("NetLogic: Couldn't initialize networking!\n");
		return(-1);
	}
	atexit(NET_Quit);

	/* Create the outbound packets */
	for ( i=0; i<2; ++i ) {
		OutBound[i] = NET_AllocPacket(BUFSIZ);
		if ( OutBound[i] == NULL ) {
			error("Out of memory (creating network buffers)\n");
			return(-1);
		}
	}

	/* Initialize network game variables */
	FoundUs   = 0;
	gOurPlayer  = -1;
	gDeathMatch = 0;
	UseServer = 0;
	for ( i=0; i<MAX_PLAYERS; ++i ) {
		GotPlayer[i] = 0;
		SyncPtrs[0][i] = NULL;
		SyncPtrs[1][i] = NULL;
	}
	OutBound[0]->buf[0] = SYNC_MSG;
	OutBound[1]->buf[0] = SYNC_MSG;
	/* Type field, frame sequence, current random seed */
	OutBound[0]->buflen = PDATA_OFFSET;
	OutBound[1]->buflen = PDATA_OFFSET;
	CurrOut = 0;

	ThisSyncs[0] = 0;
	ThisSyncs[1] = 0;
	CurrIn = 0;
	return(0);
}

void HaltNetData(void)
{
	int i;

	for ( i = 0; i < 2; ++i ) {
		if ( OutBound[i] ) {
			SDL_free(OutBound[i]);
			OutBound[i] = NULL;
		}
	}

	for ( i = 0; i < MAX_PLAYERS; ++i ) {
		if ( PlayAddr[i] ) {
			NET_UnrefAddress(PlayAddr[i]);
			PlayAddr[i] = NULL;
		}
	}

	if ( gSocket ) {
		NET_DestroyDatagramSocket(gSocket);
		gSocket = NULL;
	}

	NET_Quit();
}

int AddPlayer(const char *player)
{
	int playernum;
	int portnum;
	char *host=NULL, *port=NULL;
	char *playerstr = SDL_strdup(player);

	/* Extract host and port information */
	if ( (port=strchr(playerstr, ':')) != NULL )
		*(port++) = '\0';
	if ( (host=strchr(playerstr, '@')) != NULL )
		*(host++) = '\0';

	/* Find out which player we are referring to */
	if (((playernum = atoi(playerstr)) <= 0) || (playernum > MAX_PLAYERS)) {
		error(
"Argument to '-player' must be in integer between 1 and %d inclusive.\r\n",
								MAX_PLAYERS);
		PrintUsage();
	}

	/* Do some error checking */
	if ( GotPlayer[--playernum] ) {
		error("Player %d specified multiple times!\r\n", playernum+1);
		SDL_free(playerstr);
		return(-1);
	}
	if ( port ) {
		portnum = atoi(port);
	} else {
		portnum = NETPLAY_PORT+playernum;
	}
	if ( host ) {
		/* Resolve the remote address */
		PlayAddr[playernum] = NET_ResolveHostname(host);
		PlayPort[playernum] = portnum;
		if ( NET_WaitUntilResolved(PlayAddr[playernum], -1) != NET_SUCCESS ) {
			error("Couldn't resolve host name for %s\r\n", host);
			SDL_free(playerstr);
			return(-1);
		}
	} else { /* No host specified, local player */
		if ( FoundUs ) {
			error(
"More than one local player!  (players %d and %d specified as local players)\r\n",
						gOurPlayer+1, playernum+1);
			SDL_free(playerstr);
			return(-1);
		} else {
			gOurPlayer = playernum;
			FoundUs = 1;
			PlayAddr[playernum] = NET_ResolveHostname("localhost");
			PlayPort[playernum] = portnum;
			if ( NET_WaitUntilResolved(PlayAddr[playernum], -1) != NET_SUCCESS ) {
				error("Couldn't resolve host name for localhost\r\n");
				SDL_free(playerstr);
				return(-1);
			}
		}
	}

	/* We're done! */
	GotPlayer[playernum] = 1;
	SDL_free(playerstr);
	return(0);
}

int SetServer(const char *server)
{
	int portnum;
	char *host=NULL, *port=NULL;
	char *serverstr = SDL_strdup(server);

	/* Extract host and port information */
	if ( (host=strchr(serverstr, '@')) == NULL ) {
		error(
		"Server host must be specified in the -server option.\r\n");
		PrintUsage();
	} else
		*(host++) = '\0';
	if ( (port=strchr(serverstr, ':')) != NULL )
		*(port++) = '\0';

	/* We should know how many players we have now */
	if (((gNumPlayers = atoi(serverstr)) <= 0) ||
						(gNumPlayers > MAX_PLAYERS)) {
		error(
"The number of players must be an integer between 1 and %d inclusive.\r\n",
								MAX_PLAYERS);
		PrintUsage();
	}

	/* Resolve the remote address */
	if ( port ) {
		portnum = atoi(port);
	} else {
		portnum = NETPLAY_PORT-1;
	}
	ServAddr = NET_ResolveHostname(host);
	ServPort = portnum;
	if ( NET_WaitUntilResolved(ServAddr, -1) != NET_SUCCESS ) {
		error("Couldn't resolve host name for %s\r\n", host);
		SDL_free(serverstr);
		return(-1);
	}

	/* We're done! */
	UseServer = 1;
	SDL_free(serverstr);
	return(0);
}

/* This MUST be called after command line options have been processed. */
int CheckPlayers(void)
{
	int i;

	/* Check to make sure we have all the players */
	if ( ! UseServer ) {
		for ( i=0, gNumPlayers=0; i<MAX_PLAYERS; ++i ) {
			if ( GotPlayer[i] )
				++gNumPlayers;
		}
		/* Add ourselves if needed */
		if ( gNumPlayers == 0 ) {
			AddPlayer("1");
			gNumPlayers = 1;
			FoundUs = 1;
		}
		for ( i=0; i<gNumPlayers; ++i ) {
			if ( ! GotPlayer[i] ) {
				error(
"Player %d not specified!  Use the -player option for all players.\r\n", i+1);
				return(-1);
			}
		}
	}
	if ( ! FoundUs ) {
		error("Which player are you?  (Use the -player N option)\r\n");
		return(-1);
	}
	if ( (gOurPlayer+1) > gNumPlayers ) {
		error("You cannot be player %d in a %d player game.\r\n",
						gOurPlayer+1, gNumPlayers);
		return(-1);
	}
	if ( (gNumPlayers == 1) && gDeathMatch ) {
		error("Warning: No deathmatch in a single player game!\r\n");
		gDeathMatch = 0;
	}

	/* Oh heck, create the UDP socket here... */
	gSocket = NET_CreateDatagramSocket(NULL, PlayPort[gOurPlayer]);
	if ( gSocket == NULL ) {
		error("Couldn't create bound network socket");
		return(-1);
	}
	return(0);
}


void QueueKey(unsigned char Op, unsigned char Type)
{
	/* Drop keys on a full buffer (assumed never to really happen) */
	if ( OutLen >= (BUFSIZ-2) )
		return;

//error("Queued key 0x%.2x for frame %d\r\n", Type, NextFrame);
	OutBuf[OutLen++] = Op;
	OutBuf[OutLen++] = Type;
}

/* This function is called every frame, and is used to flush the network
   buffers, sending sync and keystroke packets.
   It is called AFTER the keyboard is polled, and BEFORE GetSyncBuf() is
   called by the player objects.

   Note:  We assume that FastRand() isn't called by an interrupt routine,
          otherwise we lose consistency.
*/
	
int SyncNetwork(void)
{
	NET_Datagram *packet;
	Uint32 seed, frame;
	int index, nleft;

	/* Set the next inbound packet buffer */
	TOGGLE(CurrIn);

	/* Set the frame number */
	frame = NextFrame;
	NET_Write32(frame, &OutBuf[1]);
	seed = GetRandSeed();
	NET_Write32(seed, &OutBuf[1+sizeof(frame)]);

	/* Send the packet to all the players */
	SendAllPlayers(OutBound[CurrOut]->buf, OutBound[CurrOut]->buflen);
	for ( nleft=0, index=0; index<gNumPlayers; ++index ) {
		if ( SyncPtr[index] == NULL ) {
			++nleft;
		}
	}
	NextSync = 0;

	/* Wait for Ack's */
	while ( nleft ) {
		int ready = NET_WaitUntilInputAvailable((void**)&gSocket, 1, 1000+60*gOurPlayer);
		if ( ready == 0 ) {
error("Timed out waiting for frame %ld\r\n", NextFrame);
			/* Timeout, resend the sync packet */
			for ( index=0; index<gNumPlayers; ++index ) {
				if ( SyncPtr[index] == NULL ) {
					SendPlayer(index, OutBound[CurrOut]->buf, OutBound[CurrOut]->buflen);
				}
			}
		}
		if ( ready <= 0 ) {
			continue;
		}

		/* We are guaranteed that there is data here */
		if ( !NET_ReceiveDatagram(gSocket, &packet) ) {
			error("Network error: NET_ReceiveDatagram()");
			return(-1);
		}
//error("Received packet!\r\n");

		/* We have a packet! */
		Uint8 *buf = packet->buf;
		if (packet->buflen == NEW_PACKETLEN && buf[0] == NEW_GAME ) {
			/* Send it back if we are not the server.. */
			if ( gOurPlayer != 0 ) {
				buf[1] = gOurPlayer;
				NET_SendDatagram(gSocket, packet->addr, packet->port, packet->buf, packet->buflen);
			}
//error("NEW_GAME packet!\r\n");
			NET_DestroyDatagram(packet);
			continue;
		}
		if ( buf[0] != SYNC_MSG ) {
			error("Unknown packet: 0x%x\n", buf[0]);
			NET_DestroyDatagram(packet);
			continue;
		}
		if ( packet->buflen < PDATA_OFFSET || packet->buflen > BUFSIZ) {
			error("Invalid packet len: %d\n", packet->buflen);
			NET_DestroyDatagram(packet);
			continue;
		}

		index = GetPlayerFromPacket(packet);
		if ( index < 0 ) {
			error("Packet from unknown source\n");
			NET_DestroyDatagram(packet);
			continue;
		}

		/* Ignore it if it is a duplicate packet */
		if ( SyncPtr[index] != NULL ) {
			NET_DestroyDatagram(packet);
			continue;
		}

		/* Check the frame number */
		frame = NET_Read32(&buf[1]);
//error("Received a packet of frame %lu from player %d\r\n", frame, index+1);
		if ( frame != NextFrame ) {
			/* We kept the last frame cached, so send it */
			if ( frame == (NextFrame-1) ) {
error("Transmitting packet for old frame (%lu)\r\n", frame);
				SendPlayer(index, OutBound[!CurrOut]->buf, OutBound[!CurrOut]->buflen);
			} else if ( frame == (NextFrame+1) ) {
error("Received packet for next frame! (%lu, current = %lu)\r\n",
						frame, NextFrame);
				/* Send this player our current frame */
				SendPlayer(index, OutBound[CurrOut]->buf, OutBound[CurrOut]->buflen);
				/* Cache this frame for next round,
				   skip consistency check, for now */
				int len = packet->buflen - PDATA_OFFSET;
				memcpy(NextBuf[NextSync], &buf[PDATA_OFFSET], len);
				NextPtr[index] = NextBuf[NextSync];
				NextLen[index] = len;
				++NextSync;
			}
else
error("Warning! Received packet for really old frame! (%lu, current = %lu)\r\n",
							frame, NextFrame);
			/* Go to select, reset timeout */
			NET_DestroyDatagram(packet);
			continue;
		}

		/* Do a consistency check!! */
		Uint32 newseed = NET_Read32(&buf[1+sizeof(frame)]);
		if ( newseed != seed ) {
//error("New seed (from player %d) is: 0x%x\r\n", index+1, newseed);
			if ( gOurPlayer == 0 ) {
				error(
"Warning!! \a Frame consistency error with player %d!! (corrected)\r\n", index+1);
SDL_Delay(3000);
			} else	/* Player 1 sent us good seed */
				SeedRandom(newseed);
		}

		/* Okay, we finally have a valid timely packet */
		int len = packet->buflen - PDATA_OFFSET;
		memcpy(SyncBuf[ThisSync], &buf[PDATA_OFFSET], len);
		SyncPtr[index] = SyncBuf[ThisSync];
		SyncLen[index] = len;
		++ThisSync;
		--nleft;

		NET_DestroyDatagram(packet);
	}

	/* Set the next outbound packet buffer */
	++NextFrame;
	TOGGLE(CurrOut);
	OutLen = PDATA_OFFSET;

	return(0);
}

/* This function retrieves a particular player's network buffer */
int GetSyncBuf(int index, unsigned char **bufptr)
{
	int retlen;

	*bufptr = SyncPtr[index];
	SyncPtr[index] = NULL;
	retlen = SyncLen[index];
	SyncLen[index] = 0;
#ifdef SERIOUS_DEBUG
if ( retlen > 0 ) {
	for ( int i=1; i<retlen; i+=2 ) {
		error(
"Keystroke (key = 0x%.2x) for player %d on frame %d!\r\n",
					(*bufptr)[i], index+1, NextFrame);
	}
}
#endif
	return(retlen);
}


inline void SuckPackets(void)
{
	NET_Datagram *packet;
	while (NET_ReceiveDatagram(gSocket, &packet) ) {
		NET_DestroyDatagram(packet);
	}
}
	

static inline void MakeNewPacket(int Wave, int Lives, int Turbo,
					unsigned char *packet)
{
	*packet++ = NEW_GAME;
	*packet++ = gOurPlayer;
	*packet++ = (unsigned char)Turbo;
	NET_Write32(Wave, packet);
	packet += 4;
	if ( gDeathMatch ) {
		Lives = (gDeathMatch|0x8000);
	}
	NET_Write32(Lives, packet);
	packet += 4;
	NET_Write32(GetRandSeed(), packet);
}

/* Flash an error up on the screen and pause for 3 seconds */
static void ErrorMessage(const char *message)
{
	/* Display the error message */
	Message(message);

	/* Wait exactly (almost) 3 seconds */
	SDL_Delay(3000);
}

/* If we use an address server, we go here, instead of using Send_NewGame()
   and Await_NewGame()

   The server simply sucks up packets until it gets all player packets.
   It then does error checking, making sure all players agree about who
   they are and how many players will be in the game.  Then it spits a
   packet containing all the player addresses to each player, and then
   waits for a new game...

   We will send a "Hi there" packet to the server and keep resending until
   either the server sends back an error packet, we get an abort signal from
   the user, or we get an addresses packet from the server.
*/
static int AlertServer(int *Wave, int *Lives, int *Turbo)
{
	NET_StreamSocket *sock;
	Uint8 netbuf[BUFSIZ], sendbuf[NEW_PACKETLEN+4+1];
	char *ptr;
	int i, len, lenread;
	Uint32 lives, seed;
	int waiting;
	int status;
	const char *message = NULL;

	/* Our address server connection is through TCP */
	Message("Connecting to Address Server");
	sock = NET_CreateClient(ServAddr, ServPort);
	if ( sock == NULL ) {
		ErrorMessage("Connection failed");
		return(-1);
	}
	if ( NET_WaitUntilConnected(sock, -1) != NET_SUCCESS ) {
		status = -1;
		message = "Connection failed";
		goto done;
	}

	MakeNewPacket(*Wave, *Lives, *Turbo, sendbuf);
	len = NEW_PACKETLEN;
	NET_Write32(PlayPort[gOurPlayer], sendbuf+len);
	len += 4;
	sendbuf[len] = (Uint8)gNumPlayers;
	len += 1;
	if ( !NET_WriteToStreamSocket(sock, sendbuf, len) ) {
		status = -1;
		message = "Socket write error";
		goto done;
	}

	Message("Waiting for other players");
	status = 0;
	len = 0;
	lenread = 0;
	waiting = 1;
	while ( waiting ) {
		if ( NET_WaitUntilInputAvailable((void **)&sock, 1, 1000) <= 0 ) {
			HandleEvents(0);
			/* Peek at key buffer for Quit key */
			for ( i=(PDATA_OFFSET+1); i<OutLen; i += 2 ) {
				if ( OutBuf[i] == ABORT_KEY ) {
					netbuf[0] = NET_ABORT;
					NET_WriteToStreamSocket(sock, netbuf, 1);
					waiting = 0;
					status = -1;
				}
			}
			OutLen = PDATA_OFFSET;
			continue;
		}

		/* We are guaranteed that there is data here */
		len = NET_ReadFromStreamSocket(sock, &netbuf[len], BUFSIZ-len-1);
		if ( len <= 0 ) {
			waiting = 0;
			status = -1;
			message = "Error reading player addresses";
			continue;
		}
		lenread += len;

		/* The very first byte is a packet length */
		if ( len < netbuf[0] )
			continue;

		if ( netbuf[0] <= 1 ) {
			waiting = 0;
			status = -1;
			message = "Error: Short server packet!";
			continue;
		}
		switch ( netbuf[1] ) {
			case NEW_GAME:	/* Extract parameters, addresses */
				*Turbo = (int)netbuf[2];
				len = 3;
				*Wave = NET_Read32(&netbuf[len]);
				len += 4;
				lives = NET_Read32(&netbuf[len]);
				len += 4;
				if ( lives & 0x8000 )
					gDeathMatch = (lives&(~0x8000));
				else
					*Lives = lives;
				seed = NET_Read32(&netbuf[len]);
				len += 4;
				SeedRandom(seed);
//error("Seed is 0x%x\r\n", seed);

				ptr = (char *)&netbuf[len];
				for ( i=0; i<gNumPlayers; ++i ) {
					if ( i == gOurPlayer ) {
						/* Skip address */
						ptr += (strlen(ptr)+1);
						ptr += (strlen(ptr)+1);
						continue;
					}

					/* Resolve the remote address */
					char *host, *port;
					host = ptr;
					ptr += strlen(host)+1;
					port = ptr;
					ptr += strlen(port)+1;
					PlayAddr[i] = NET_ResolveHostname(host);
					PlayPort[i] = atoi(port);
//printf("Port = %s\r\n", ptr);
				}
				waiting = 0;
				break;

			case NET_ABORT:	/* Some error? */
				netbuf[len] = '\0';
				message = (char *)&netbuf[2];
				waiting = 0;
				status = -1;
				break;

			default:	/* Huh? */
				break;
		}
	}
	NextFrame = 0L;
done:
	NET_DestroyStreamSocket(sock);
	if ( (status < 0) && message ) {
		ErrorMessage(message);
	}
	return(status);
}

/* This function sends a NEWGAME packet, and waits for all other players
   to respond in kind.
   This function is not very robust in handling errors such as multiple
   machines thinking they are the same player.  The address server is
   supposed to handle such things gracefully.
*/
int Send_NewGame(int *Wave, int *Lives, int *Turbo)
{
	Uint8 newgame[NEW_PACKETLEN];
	char message[BUFSIZ];
	int  nleft, n;
	int  acked[MAX_PLAYERS];
	int  i;
	NET_Datagram *packet;

	/* Don't do the usual rigamarole if we have a game server */
	if ( UseServer )
		return(AlertServer(Wave, Lives, Turbo));

	/* Send all the packets */
	MakeNewPacket(*Wave, *Lives, *Turbo, newgame);
	SendAllPlayers(newgame, sizeof(newgame));

	/* Get ready for responses */
	memset(acked, 0, (sizeof acked));

	/* Wait for Ack's */
	for ( nleft=gNumPlayers, n=0; nleft; ) {
		/* Show a status */
		SDL_strlcpy(message, "Waiting for players:", sizeof(message));
		for ( i=0; i<gNumPlayers; ++i ) {
			if ( ! acked[i] )
				SDL_snprintf(&message[strlen(message)], sizeof(message)-strlen(message), " %d", i+1);
		}
		Message(message);

		if ( NET_WaitUntilInputAvailable((void**)&gSocket, 1, 1000) <= 0 ) {
			HandleEvents(0);
			/* Peek at key buffer for Quit key */
			for ( i=(PDATA_OFFSET+1); i<OutLen; i += 2 ) {
				if ( OutBuf[i] == ABORT_KEY ) {
					OutLen = PDATA_OFFSET;
					return(-1);
				}
			}
			OutLen = PDATA_OFFSET;

			/* Every three seconds...resend the new game packet */
			if ( (n++)%3 != 0 )
				continue;

			for ( i=gNumPlayers; i--; ) {
				if ( ! acked[i] ) {
					SendPlayer(i, newgame, sizeof(newgame));
				}
			}
			continue;
		}

		/* We are guaranteed that there is data here */
		if ( !NET_ReceiveDatagram(gSocket, &packet) ) {
			ErrorMessage("Network error receiving packets");
			return(-1);
		}

		/* We have a packet! */
		const Uint8 *netbuf = packet->buf;
		if ( netbuf[0] != NEW_GAME ) {
			/* Continue waiting */
#ifdef VERBOSE
			error("Unknown packet: 0x%x\r\n", netbuf[0]);
#endif
			NET_DestroyDatagram(packet);
			continue;
		}

		/* Loop, check the address */
		for ( i=gNumPlayers; i--; ) {
			if ( acked[i] )
				continue;

			/* Check both the host AND port!! :-) */
			if ( NET_CompareAddresses(packet->addr, PlayAddr[i]) != 0 ||
			     packet->port != PlayPort[i] )
				continue;

			/* Check the player... */
			if ( (i != gOurPlayer) && (netbuf[1] == gOurPlayer) ) {
				/* Print message, sleep 3 seconds absolutely */
				SDL_snprintf(message, sizeof(message),
	"Error: Another player (%d) thinks they are player 1!\r\n", i+1);
				ErrorMessage(message);
				/* Suck up retransmission packets */
				SuckPackets();
				NET_DestroyDatagram(packet);
				return(-1);
			}

			/* Check them off our list.. */
			--nleft;
			acked[i] = 1;
			break;
		}
		NET_DestroyDatagram(packet);
	}
	NextFrame = 0L;
	return(0);
}

int Await_NewGame(int *Wave, int *Lives, int *Turbo)
{
	int len, gameon;
	NET_Datagram *packet;
	Uint32 lives, seed;

	/* Don't do the usual rigamarole if we have a game server */
	if ( UseServer )
		return(AlertServer(Wave, Lives, Turbo));

	/* Get ready to wait for server */
	Message("Awaiting Player 1 (server)");

	gameon = 0;
	while ( ! gameon ) {
		if ( NET_WaitUntilInputAvailable((void**)&gSocket, 1, 1000) <= 0 ) {
			HandleEvents(0);
			/* Peek at key buffer for Quit key */
			for ( int i=(PDATA_OFFSET+1); i<OutLen; i += 2 ) {
				if ( OutBuf[i] == ABORT_KEY ) {
					OutLen = PDATA_OFFSET;
					return(-1);
				}
			}
			OutLen = PDATA_OFFSET;
			continue;
		}

		/* We are guaranteed that there is data here */
		if ( !NET_ReceiveDatagram(gSocket, &packet) ) {
			ErrorMessage("Network error receiving packets");
			return(-1);
		}

		/* We have a packet! */
		Uint8* netbuf = packet->buf;
		if ( netbuf[0] != NEW_GAME ) {
#ifdef VERBOSE
			error(
			"Await_NewGame(): Unknown packet: 0x%x\r\n", netbuf[0]);
#endif
			NET_DestroyDatagram(packet);
			continue;
		}

		/* Extract the RandomSeed and return the packet */
		*Turbo = (int)netbuf[2];
		len = 3;
		*Wave = NET_Read32(&netbuf[len]);
		len += 4;
		lives = NET_Read32(&netbuf[len]);
		len += 4;
		if ( lives & 0x8000 )
			gDeathMatch = (lives&(~0x8000));
		else
			*Lives = lives;
		seed = NET_Read32(&netbuf[len]);
		len += 4;
		SeedRandom(seed);
//error("Seed is 0x%x\r\n", seed);

		netbuf[1] = gOurPlayer;
		SendPlayer(0, packet->buf, packet->buflen);

		/* Note that we don't guarantee delivery of the NEW_GAME ack.
		   That's okay, we have the checksum.  We will hang on the very
		   first frame, and we echo back all NEW_GAME packets at that
		   point as well.
		*/
		NextFrame = 0L;
		gameon = 1;

		NET_DestroyDatagram(packet);
	}
	return(0);
}
