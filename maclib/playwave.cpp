/*
    PLAYWAVE:  A WAVE file player using the maclib and SDL libraries
    Copyright (C) 1997-2021 Sam Lantinga <slouken@libsdl.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Very simple WAVE player */

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include "Mac_Wave.h"

static struct {
	SDL_Semaphore *done;
	Wave *wave;
	Uint8 silence;
	SDL_AudioStream *audio_stream;
} globals;

static void SDLCALL fillerup(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	Wave *wave = (Wave *)userdata;
    Uint32 data_left = wave->DataLeft();
    if (additional_amount > 0) {
		int count_bytes = SDL_min(total_amount, (int)data_left);
		SDL_PutAudioStreamData(stream, wave->Data(), count_bytes);
		wave->Forward(count_bytes);
	}
	if (data_left == 0) {
		SDL_SignalSemaphore(globals.done);
	}
}

static void CleanUp(int status)
{
	SDL_DestroyAudioStream(globals.audio_stream);
	SDL_DestroySemaphore(globals.done);
	SDL_Quit();
	delete globals.wave;
	exit(status);
}

int main(int argc, char *argv[])
{
	Mac_Resource *macx;
	Mac_ResData  *snd;
	SDL_AudioSpec *spec;
	Uint16        rate;

	if ( !SDL_Init(SDL_INIT_AUDIO) ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		exit(1);
	}
	rate = 0;
	if ( (argc >= 3) && (strcmp(argv[1], "-rate") == 0) ) {
		int i;
		rate = (Uint16)atoi(argv[2]);
		for ( i=3; argv[i]; ++i ) {
			argv[i-2] = argv[i];
		}
		argv[i-2] = NULL;
		argc -= 2;
	}
	switch (argc) {
		case 2:
			/* Load the wave file into memory */
			globals.wave = new Wave(argv[1], rate);
			if ( globals.wave->Error() ) {
				fprintf(stderr, "%s\n", globals.wave->Error());
				delete globals.wave;
				exit(255);
			}
			break;
		case 3:
			macx = new Mac_Resource(argv[1]);
			if ( macx->Error() ) {
				fprintf(stderr, "%s\n", macx->Error());
				delete macx;
				exit(255);
			}
			if ( (argv[2][0] >= '0') && (argv[2][0] <= '9') )
				snd = macx->Resource("snd ", atoi(argv[2]));
			else
				snd = macx->Resource("snd ", argv[2]);
			if ( snd == NULL ) {
				fprintf(stderr, "%s\n", macx->Error());
				delete macx;
				exit(255);
			}
			globals.wave = new Wave(snd, rate);
			delete macx;
			if ( globals.wave->Error() ) {
				fprintf(stderr, "%s\n", globals.wave->Error());
				delete globals.wave;
				exit(255);
			}
			break;
		default:
	fprintf(stderr, "Usage: %s [-rate <rate>] <wavefile>\n", argv[0]);
	fprintf(stderr, "or..\n");
	fprintf(stderr, "       %s [-rate <rate>] <snd_fork> [soundnum]\n",
								argv[0]);
			exit(1);
	}
	spec = globals.wave->Spec();
	globals.silence = ((spec->format&SDL_AUDIO_U8) ? 0x80 : 0x00);

	globals.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec, fillerup, globals.wave);
	if ( globals.audio_stream == NULL ) {
		fprintf(stderr, "%s\n", SDL_GetError());
		CleanUp(255);
	}

#ifdef SAVE_THE_WAVES
	if ( globals.wave->Save("save.wav") < 0 )
		fprintf(stderr, "Warning: %s\n", globals.wave->Error());
#endif

	/* Create a semaphore to wait for end of play */
	globals.done=SDL_CreateSemaphore(1);
	if ( globals.done == NULL ) {
		fprintf(stderr, "%s\n", SDL_GetError());
		SDL_Quit();
		exit(255);
	}
    SDL_WaitSemaphore(globals.done);	/* Prime it for blocking */

	/* Set the signals */
#ifdef SIGHUP
	signal(SIGHUP, CleanUp);
#endif
	signal(SIGINT, CleanUp);
#ifdef SIGQUIT
	signal(SIGQUIT, CleanUp);
#endif
	signal(SIGTERM, CleanUp);

	/* Show what audio format we're playing */
	printf("Playing %#.2f seconds (%d bit %s) at %u Hz\n", 
		(double)(globals.wave->DataLeft()/globals.wave->SampleSize())/globals.wave->Frequency(),
			globals.wave->BitsPerSample(),
			globals.wave->Stereo() ? "stereo" : "mono", globals.wave->Frequency());

	/* Start the audio device */
	SDL_ResumeAudioStreamDevice(globals.audio_stream);

	/* Waiting until finished */
	SDL_WaitSemaphore(globals.done);

	/* We're done! */
	CleanUp(0);

	/* Not reached */
	return 0;
}
