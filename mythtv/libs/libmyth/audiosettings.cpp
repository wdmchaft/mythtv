/* -*- Mode: c++ -*-
 *
 * Copyright (C) Daniel Kristjansson 2008
 *
 * Licensed under the GPL v2 or a later version at your choosing.
 */

#include "audiosettings.h"

// startup_upmixer 
AudioSettings::AudioSettings() :
    main_device(QString::null),
    passthru_device(QString::null),
    format(FORMAT_NONE),
    channels(-1),
    codec(0),
    samplerate(-1),
    set_initial_vol(false),
    use_passthru(false),
    source(AUDIOOUTPUT_UNKNOWN),
    upmixer(0)
{
}

AudioSettings::AudioSettings(const AudioSettings &other) :
    main_device(other.main_device),
    passthru_device(other.passthru_device),
    format(other.format),
    channels(other.channels),
    codec(other.codec),
    samplerate(other.samplerate),
    set_initial_vol(other.set_initial_vol),
    use_passthru(other.use_passthru),
    source(other.source),
    upmixer(other.upmixer)
{
}

AudioSettings::AudioSettings(
    const QString     &main_device,
    const QString     &passthru_device,
    AudioFormat       format,
    int               channels,
    int               codec,
    int               samplerate,
    AudioOutputSource source,
    bool              set_initial_vol,
    bool              use_passthru,
    int               upmixer_startup) :
    main_device(main_device),
    passthru_device(passthru_device),
    format(format),
    channels(channels),
    codec(codec),
    samplerate(samplerate),
    set_initial_vol(set_initial_vol),
    use_passthru(use_passthru),
    source(source),
    upmixer(upmixer_startup)
{
}

AudioSettings::AudioSettings(
    AudioFormat format,
    int         channels,
    int         codec,
    int         samplerate,
    bool        use_passthru,
    int         upmixer_startup) :
    main_device(QString::null),
    passthru_device(QString::null),
    format(format),
    channels(channels),
    codec(codec),
    samplerate(samplerate),
    set_initial_vol(false),
    use_passthru(use_passthru),
    source(AUDIOOUTPUT_UNKNOWN),
    upmixer(upmixer_startup)
{
}

void AudioSettings::FixPassThrough(void)
{
    if (passthru_device.isEmpty() || passthru_device.toLower() == "default")
        passthru_device = GetMainDevice();
}

void AudioSettings::TrimDeviceType(void)
{
    main_device.remove(0, 5);
    passthru_device.remove(0, 5);
}

QString AudioSettings::GetMainDevice(void) const
{
    QString ret = main_device;
    ret.detach();
    return ret;
}

QString AudioSettings::GetPassthruDevice(void) const
{
    QString ret = passthru_device;
    ret.detach();
    return ret;
}

