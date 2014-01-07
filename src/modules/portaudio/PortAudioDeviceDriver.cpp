// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2013 iCub Facility - Istituto Italiano di Tecnologia
 * Authors: Marco Randazzo, Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include <PortAudioDeviceDriver.h>

#include <stdio.h>
#include <stdlib.h>
#include <portaudio.h>

#include <yarp/os/Time.h>

using namespace yarp::os;
using namespace yarp::dev;

#define SLEEP_TIME 0.005f

/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int bufferIOCallback( const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData )
{
    circularDataBuffers* dataBuffers = static_cast<circularDataBuffers*>(userData);
    circularBuffer *playdata = dataBuffers->playData;
    circularBuffer *recdata  = dataBuffers->recData;
    int finished = paComplete;

    if (dataBuffers->canRec)
    {
        const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
        unsigned int framesToCalc;
        unsigned int i;
        unsigned long framesLeft = recdata->getMaxSize()-recdata->size();

        (void) outputBuffer; // just to prevent unused variable warnings
        (void) timeInfo;
        (void) statusFlags;
        (void) userData;

        if( framesLeft/NUM_CHANNELS < framesPerBuffer )
        {
            framesToCalc = framesLeft/NUM_CHANNELS;
            finished = paComplete;
        }
        else
        {
            framesToCalc = framesPerBuffer;
            finished = paContinue;
        }

        if( inputBuffer == NULL )
        {
            for( i=0; i<framesToCalc; i++ )
            {
                recdata->write(0); // left
                if( NUM_CHANNELS == 2 ) recdata->write(0);  // right
            }
        }
        else
        {
            for( i=0; i<framesToCalc; i++ )
            {
                recdata->write(*rptr++);  // left
                if( NUM_CHANNELS == 2 ) recdata->write(*rptr++);  // right
            }
        }
        //note: you can record or play but not simultaneously (for now)
        return finished;
    }

    if (dataBuffers->canPlay)
    {
        SAMPLE *wptr = (SAMPLE*)outputBuffer;
        unsigned int i;

        unsigned int framesLeft = playdata->size();

        (void) inputBuffer; // just to prevent unused variable warnings
        (void) timeInfo;
        (void) statusFlags;
        (void) userData;

        if( framesLeft/NUM_CHANNELS < framesPerBuffer )
        {
            // final buffer
            for( i=0; i<framesLeft/NUM_CHANNELS; i++ )
            {
                *wptr++ = playdata->read();  // left 
                if( NUM_CHANNELS == 2 ) *wptr++ = playdata->read();  // right 
            }
            for( ; i<framesPerBuffer; i++ )
            {
                *wptr++ = 0;  // left 
                if( NUM_CHANNELS == 2 ) *wptr++ = 0;  // right 
            }
            finished = paComplete;
        }
        else
        {
            for( i=0; i<framesPerBuffer; i++ )
            {
                *wptr++ = playdata->read();  // left 
                if( NUM_CHANNELS == 2 ) *wptr++ = playdata->read();  // right 
            }
            finished = paContinue;
        }
        //note: you can record or play but not simultaneously (for now)
        return finished;
    }

    printf("No read/write operations requested, aborting\n");
    return paAbort;
}

PortAudioDeviceDriver::PortAudioDeviceDriver()
{
    system_resource = NULL;
    numSamples = 0;
    numChannels = 0;
    loopBack = false;
    set_freq = 0;
    err = paNoError;
    dataBuffers.playData = 0;
    dataBuffers.recData = 0;
    renderMode = RENDER_APPEND;
}

PortAudioDeviceDriver::~PortAudioDeviceDriver()
{
    close();
}


bool PortAudioDeviceDriver::open(yarp::os::Searchable& config)
{
    driverConfig.rate = config.check("rate",Value(0),"audio sample rate (0=automatic)").asInt();
    driverConfig.samples = config.check("samples",Value(0),"number of samples per network packet (0=automatic)").asInt();
    driverConfig.channels = config.check("channels",Value(0),"number of audio channels (0=automatic, max is 2)").asInt();
    driverConfig.wantRead = (bool)config.check("read","if present, just deal with reading audio (microphone)");
    driverConfig.wantWrite = (bool)config.check("write","if present, just deal with writing audio (speaker)");
    driverConfig.deviceNumber = config.check("id",Value(-1),"which portaudio index to use (-1=automatic)").asInt();
    
    if (!(driverConfig.wantRead||driverConfig.wantWrite))
    {
        driverConfig.wantRead = driverConfig.wantWrite = true;
    }

    if (config.check("loopback","if present, send audio read from microphone immediately back to speaker"))
    {
        printf ("WARN: loopback not yet implemented\n");
        loopBack = true;
    }

    if (config.check("render_mode_append"))
    {
        renderMode = RENDER_APPEND;
    }
    if (config.check("render_mode_immediate"))
    {
        renderMode = RENDER_IMMEDIATE;
    }

    return open(driverConfig);
}

bool PortAudioDeviceDriver::open(PortAudioDeviceDriverSettings& config)
{
    int rate = config.rate;
    int samples = config.samples;
    int channels = config.channels;
    bool wantRead = config.wantRead;
    bool wantWrite = config.wantWrite;
    int deviceNumber = config.deviceNumber;
    if (rate==0)    rate = SAMPLE_RATE;
    if (samples==0) samples = 30 * SAMPLE_RATE * NUM_CHANNELS; //30seconds
    numSamples = samples;
    if (channels==0) channels = NUM_CHANNELS;
    numChannels = channels;
    set_freq = rate;

    //buffer.allocate(num_samples*num_channels*sizeof(SAMPLE));
    numBytes = numSamples * sizeof(SAMPLE);
    if (dataBuffers.playData==0)
        dataBuffers.playData = new circularBuffer(numBytes);
    if (dataBuffers.recData==0)
        dataBuffers.recData = new circularBuffer(numBytes);
    if (wantRead) dataBuffers.canRec = true;
    if (wantWrite) dataBuffers.canPlay = true;

    err = Pa_Initialize();
    if( err != paNoError ) {
        printf("portaudio system failed to initialize\n");
        return false;
    }

    inputParameters.device = (deviceNumber==-1)?Pa_GetDefaultInputDevice():deviceNumber;
    printf("Device number %d\n", inputParameters.device);
    inputParameters.channelCount = numChannels;
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    if ((Pa_GetDeviceInfo( inputParameters.device ))!=0) {
        inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    }
    inputParameters.hostApiSpecificStreamInfo = NULL;

    outputParameters.device = (deviceNumber==-1)?Pa_GetDefaultOutputDevice():deviceNumber;
    outputParameters.channelCount = numChannels;
    outputParameters.sampleFormat = PA_SAMPLE_TYPE;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
              &stream,
              wantRead?(&inputParameters):NULL,
              wantWrite?(&outputParameters):NULL,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      
              bufferIOCallback,
              &dataBuffers );

    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          // Always return 0 or 1, but no other return codes. 
    }

    //start the thread
    pThread.stream = stream;
    pThread.start();

    return (err==paNoError);
}

void streamThread::handleError()
{
    Pa_Terminate();
    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          // Always return 0 or 1, but no other return codes. 
    }
}

void PortAudioDeviceDriver::handleError()
{
    Pa_Terminate();
    dataBuffers.playData->clear();

    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          // Always return 0 or 1, but no other return codes. 
    }
}

bool PortAudioDeviceDriver::close(void)
{
    err = Pa_CloseStream( stream );
    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          // Always return 0 or 1, but no other return codes. 
    }
    return (err==paNoError);
}

bool PortAudioDeviceDriver::getSound(yarp::sig::Sound& sound)
{
    pThread.something_to_record = true;
    
    //this is blocking: wait until acquisition is done
    while (pThread.something_to_record == true)
    {
         yarp::os::Time::delay(SLEEP_TIME);
    }

    if (sound.getChannels()!=this->numChannels && sound.getSamples() != this->numSamples)
    {
        sound.resize(this->numSamples,this->numChannels);
    }

    for (int i=0; i<this->numSamples; i++)
        for (int j=0; j<this->numChannels; j++)
            {
                SAMPLE s = dataBuffers.recData->read();
                sound.set(s,i,j);
            }
    return true;
}

bool PortAudioDeviceDriver::abortSound(void)
{
    printf("\n=== Stopping and clearing stream.===\n"); fflush(stdout);
    err = Pa_StopStream( stream );
    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          // Always return 0 or 1, but no other return codes. 
    }

    dataBuffers.playData->clear();

    return (err==paNoError);
}

void streamThread::threadRelease()
{
}

bool streamThread::threadInit()
{
    something_to_play=false;
    something_to_record=false;
    err = paNoError;
    return true;
}

void streamThread::run()
{
    while(1)
    {
        if( something_to_play )
        {
            something_to_play = false;
            err = Pa_StartStream( stream );
            if( err != paNoError ) {handleError(); return;}
        
            while( ( err = Pa_IsStreamActive( stream ) ) == 1 )
                {yarp::os::Time::delay(SLEEP_TIME);}
            if( err < 0 ) {handleError(); return;}

            err = Pa_StopStream( stream );
            //err = Pa_AbortStream( stream );
            if( err < 0 ) {handleError(); return;}
        
        }
        
        if (something_to_record)
        {
            something_to_record = false;
            err = Pa_StartStream( stream );
            if( err != paNoError ) {handleError(); return;}
        
            while( ( err = Pa_IsStreamActive( stream ) ) == 1 ) 
                {yarp::os::Time::delay(SLEEP_TIME);}
            if( err < 0 ) {handleError(); return;}

            err = Pa_StopStream( stream );
            //err = Pa_AbortStream( stream );
            if( err < 0 ) {handleError(); return;}
        }

        yarp::os::Time::delay(SLEEP_TIME);
    }
    return;
}

bool PortAudioDeviceDriver::immediateSound(yarp::sig::Sound& sound)
{
    dataBuffers.playData->clear();
    
    unsigned char* dataP= sound.getRawData();
    int num_bytes = sound.getBytesPerSample();
    int num_channels = sound.getChannels();
    int num_samples = sound.getRawDataSize()/num_channels/num_bytes;
    // memcpy(data.samplesBuffer,dataP,num_samples/**num_bytes*num_channels*/);
    
    for (int i=0; i<num_samples; i++)
        for (int j=0; j<num_channels; j++)
            dataBuffers.playData->write (sound.get(i,j));

    pThread.something_to_play = true;
    return true;
}

bool PortAudioDeviceDriver::renderSound(yarp::sig::Sound& sound)
{
    if (renderMode == RENDER_IMMEDIATE)
        return immediateSound(sound);
    else if (renderMode == RENDER_APPEND)
        return appendSound(sound);

    return false;
}

bool PortAudioDeviceDriver::appendSound(yarp::sig::Sound& sound)
{
    unsigned char* dataP= sound.getRawData();
    int num_bytes = sound.getBytesPerSample();
    int num_channels = sound.getChannels();
    int num_samples = sound.getRawDataSize()/num_channels/num_bytes;
    // memcpy(data.samplesBuffer,dataP,num_samples/**num_bytes*num_channels*/);
    
    for (int i=0; i<num_samples; i++)
        for (int j=0; j<num_channels; j++)
            dataBuffers.playData->write (sound.get(i,j));

    pThread.something_to_play = true;
    return true;
}


