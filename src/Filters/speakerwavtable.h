/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakerwavtable.h

Abstract:

    Declaration of wave miniport tables for the render endpoints.
--*/

#ifndef _SIMPLEAUDIOSAMPLE_SPEAKERWAVTABLE_H_
#define _SIMPLEAUDIOSAMPLE_SPEAKERWAVTABLE_H_

// To keep the code simple assume device supports only 48KHz, 16-bit, stereo (PCM and NON-PCM)

#define SPEAKER_DEVICE_MAX_CHANNELS                 8       // Max Channels.

#define SPEAKER_HOST_MAX_CHANNELS                   8       // Max Channels.
#define SPEAKER_HOST_MIN_BITS_PER_SAMPLE            16      // Min Bits Per Sample
#define SPEAKER_HOST_MAX_BITS_PER_SAMPLE            32      // Max Bits Per Sample
#define SPEAKER_HOST_MIN_SAMPLE_RATE                44100   // Min Sample Rate
#define SPEAKER_HOST_MAX_SAMPLE_RATE                192000  // Max Sample Rate

//
// Max # of pin instances.
//
#define SPEAKER_MAX_INPUT_SYSTEM_STREAMS            1

//=============================================================================

// The sample offered 48 kHz 16-bit stereo and nothing else. A test endpoint has to be able to say yes and no to
// more than that: exclusive-mode clients negotiate against this list, and the shared-mode mix format is chosen
// from it. PCM throughout; stereo at every common rate and depth, 5.1 (both layouts) and 7.1 at 48 and 96 kHz.
// The miniport matches the channel mask exactly, which is why 5.1 appears twice.
#define VAUDIO_PCM_FORMAT(rate, bits, channels, mask) \
    { \
        { \
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE), \
            0, \
            0, \
            0, \
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO), \
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM), \
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) \
        }, \
        { \
            { \
                WAVE_FORMAT_EXTENSIBLE, \
                (channels), \
                (rate), \
                (rate) * (channels) * ((bits) / 8), \
                (channels) * ((bits) / 8), \
                (bits), \
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) \
            }, \
            (bits), \
            (mask), \
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM) \
        } \
    }

static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerHostPinSupportedDeviceFormats[] =
{
    VAUDIO_PCM_FORMAT(48000, 16, 2, KSAUDIO_SPEAKER_STEREO),   // 0: the default, as in the sample
    VAUDIO_PCM_FORMAT(44100, 16, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(44100, 24, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(44100, 32, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(48000, 24, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(48000, 32, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(88200, 16, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(88200, 24, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(88200, 32, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(96000, 16, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(96000, 24, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(96000, 32, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(176400, 16, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(176400, 24, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(176400, 32, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(192000, 16, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(192000, 24, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(192000, 32, 2, KSAUDIO_SPEAKER_STEREO),
    VAUDIO_PCM_FORMAT(48000, 16, 6, KSAUDIO_SPEAKER_5POINT1),
    VAUDIO_PCM_FORMAT(48000, 24, 6, KSAUDIO_SPEAKER_5POINT1),
    VAUDIO_PCM_FORMAT(48000, 32, 6, KSAUDIO_SPEAKER_5POINT1),
    VAUDIO_PCM_FORMAT(96000, 16, 6, KSAUDIO_SPEAKER_5POINT1),
    VAUDIO_PCM_FORMAT(96000, 24, 6, KSAUDIO_SPEAKER_5POINT1),
    VAUDIO_PCM_FORMAT(96000, 32, 6, KSAUDIO_SPEAKER_5POINT1),
    VAUDIO_PCM_FORMAT(48000, 16, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(48000, 24, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(48000, 32, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(96000, 16, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(96000, 24, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(96000, 32, 6, KSAUDIO_SPEAKER_5POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(48000, 16, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(48000, 24, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(48000, 32, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(96000, 16, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(96000, 24, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND),
    VAUDIO_PCM_FORMAT(96000, 32, 8, KSAUDIO_SPEAKER_7POINT1_SURROUND),
};

//
// Supported modes (only on streaming pins).
//
static
MODE_AND_DEFAULT_FORMAT SpeakerHostPinSupportedDeviceModes[] =
{
    {
        STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT,
        &SpeakerHostPinSupportedDeviceFormats[0].DataFormat  // 48KHz
    }
};

//
// The entries here must follow the same order as the filter's pin
// descriptor array.
//
static 
PIN_DEVICE_FORMATS_AND_MODES SpeakerPinDeviceFormatsAndModes[] = 
{
    {
        SystemRenderPin,
        SpeakerHostPinSupportedDeviceFormats,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceFormats),
        SpeakerHostPinSupportedDeviceModes,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceModes)
    },
    {
        BridgePin,
        NULL,
        0,
        NULL,
        0
    }
};

//=============================================================================
static
KSDATARANGE_AUDIO SpeakerPinDataRangesStream[] =
{
    { // 0
        {
            sizeof(KSDATARANGE_AUDIO),
            KSDATARANGE_ATTRIBUTES,         // An attributes list follows this data range
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        SPEAKER_HOST_MAX_CHANNELS,           
        SPEAKER_HOST_MIN_BITS_PER_SAMPLE,    
        SPEAKER_HOST_MAX_BITS_PER_SAMPLE,    
        SPEAKER_HOST_MIN_SAMPLE_RATE,            
        SPEAKER_HOST_MAX_SAMPLE_RATE             
    }
};

static
PKSDATARANGE SpeakerPinDataRangePointersStream[] =
{
    PKSDATARANGE(&SpeakerPinDataRangesStream[0]),
    PKSDATARANGE(&PinDataRangeAttributeList),
};

//=============================================================================
static
KSDATARANGE SpeakerPinDataRangesBridge[] =
{
    {
        sizeof(KSDATARANGE),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
    }
};

static
PKSDATARANGE SpeakerPinDataRangePointersBridge[] =
{
    &SpeakerPinDataRangesBridge[0]
};

//=============================================================================
static
PCPIN_DESCRIPTOR SpeakerWaveMiniportPins[] =
{
    // Wave Out Streaming Pin (Renderer) KSPIN_WAVE_RENDER3_SINK_SYSTEM
    {
        SPEAKER_MAX_INPUT_SYSTEM_STREAMS,
        SPEAKER_MAX_INPUT_SYSTEM_STREAMS, 
        0,
        NULL,        // AutomationTable
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersStream),
            SpeakerPinDataRangePointersStream,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    // Wave Out Bridge Pin (Renderer) KSPIN_WAVE_RENDER3_SOURCE
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersBridge),
            SpeakerPinDataRangePointersBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
};

//=============================================================================
//
//                   ----------------------------      
//                   |                          |      
//  Host Pin     0-->|                          |--> 1 KSPIN_WAVE_RENDER3_SOURCE
//                   |                          |      
//                   ----------------------------
static
PCCONNECTION_DESCRIPTOR SpeakerWaveMiniportConnections[] =
{
    { PCFILTER_NODE,            KSPIN_WAVE_RENDER3_SINK_SYSTEM,     PCFILTER_NODE,   KSPIN_WAVE_RENDER3_SOURCE }
};

//=============================================================================
static
PCPROPERTY_ITEM PropertiesSpeakerWaveFilter[] =
{
    {
        &KSPROPSETID_Pin,
        KSPROPERTY_PIN_PROPOSEDATAFORMAT,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_Pin,
        KSPROPERTY_PIN_PROPOSEDATAFORMAT2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(AutomationSpeakerWaveFilter, PropertiesSpeakerWaveFilter);

//=============================================================================
static
PCFILTER_DESCRIPTOR SpeakerWaveMiniportFilterDescriptor =
{
    0,                                              // Version
    &AutomationSpeakerWaveFilter,                   // AutomationTable
    sizeof(PCPIN_DESCRIPTOR),                       // PinSize
    SIZEOF_ARRAY(SpeakerWaveMiniportPins),          // PinCount
    SpeakerWaveMiniportPins,                        // Pins
    sizeof(PCNODE_DESCRIPTOR),                      // NodeSize
    0,                                              // NodeCount
    NULL,                                           // Nodes
    SIZEOF_ARRAY(SpeakerWaveMiniportConnections),   // ConnectionCount
    SpeakerWaveMiniportConnections,                 // Connections
    0,                                              // CategoryCount
    NULL                                            // Categories  - use defaults (audio, render, capture)
};

#endif // _SIMPLEAUDIOSAMPLE_SPEAKERWAVTABLE_H_
