//
//  processing.c
//  oscituner
//
//  Created by Denis Kreshikhin on 29.12.14.
//  Copyright (c) 2014 Denis Kreshikhin. All rights reserved.
//

#include "processing.h"
#include "dspmath.h"

#include <stdlib.h>
#include <math.h>
#include <string.h> /* memset */
#include <unistd.h> /* close */

//double processing_detect_undertone(Processing* p, double f0);

float* build_edge(float* dest, float x0, float y0, float x1, float y1, float thickness);
float* write_point2D(float* dest, float x, float y);
float* write_point4D(float* dest, float x, float y, float z, float d);

double data_avr(double* data, size_t length);
void data_shift(double* data, size_t length, double shift);
double data_max(double* data, size_t length);
double data_min(double* data, size_t length);
double data_dev(double* data, size_t length);
void data_scale(double* data, size_t length, double scale);

void data_append_and_shift(double* data, size_t length, double value);

double processing_clarify_peak_frequency_in_range(Processing* p, double range);

Processing* processing_create(){
    return malloc(sizeof(Processing));
}

void processing_destroy(Processing* p){
    free(p);
}

void processing_init(Processing* p, double fd, double fMin, size_t sampleCount, size_t pointCount, size_t previewLength) {
    p->fd = fd;
    p->fMin = fMin;
    
    p->targetFrequency = 440;
    p->targetHarmonic = 1;
    p->filter = false;
    
    p->peakFrequency = 440;
    p->peakPhase = 0;
    
    p->signalLength = ceil2((double)sampleCount);
    p->step = 1;
    
    p->subcounter = 0;
    p->subLength = 32;
    p->subSignalReal = malloc(p->subLength * sizeof(*p->subSignalReal));
    p->subSignalImag = malloc(p->subLength * sizeof(*p->subSignalImag));
    p->subSpectrumReal = malloc(p->subLength * sizeof(*p->subSpectrumReal));
    p->subSpectrumImag = malloc(p->subLength * sizeof(*p->subSpectrumImag));
    p->subSpectrum = malloc(p->subLength * sizeof(*p->subSpectrum));
    
    p->pointCount = pointCount;
    p->points = malloc(p->pointCount * sizeof(*p->points));
    
    p->signal = malloc(p->signalLength * sizeof(*p->signal));
    
    p->previewLength = previewLength;
    p->preview = malloc(p->previewLength * sizeof(*p->preview));

    memset(p->signal, 0, p->signalLength * sizeof(*p->signal));

    p->real = malloc(p->signalLength * sizeof(*p->real));
    p->imag = malloc(p->signalLength * sizeof(*p->imag));
    
    p->spectrum = malloc(p->signalLength * sizeof(*p->spectrum));
    
    p->fs = vDSP_create_fftsetupD(log2(p->signalLength), kFFTRadix2);
    p->sfs = vDSP_create_fftsetupD(log2(p->subLength), kFFTRadix2);
}

void processing_deinit(Processing* p){
    vDSP_destroy_fftsetupD(p->fs);
    vDSP_destroy_fftsetupD(p->sfs);
    
    free(p->signal);
    free(p->preview);
    free(p->real);
    free(p->imag);
    free(p->spectrum);
    free(p->points);
    
    free(p->subSignalImag);
    free(p->subSignalReal);
    free(p->subSpectrumImag);
    free(p->subSpectrumReal);
    free(p->subSpectrum);
}

void processing_set_target_frequency(Processing* p, double frequency, int targetHarmonic) {
    p->targetFrequency = frequency;
    p->targetHarmonic = targetHarmonic;
}

void processing_enable_filter(Processing* p) {
    p->filter = true;
}

void processing_disable_filter(Processing* p) {
    p->filter = false;
}


void processing_push(Processing* p, const double* packet, size_t packetLength) {
    long int shift = p->signalLength - packetLength;
    
    if(shift <= 0) {
        memcpy(p->signal,
               packet - shift,
               p->signalLength * sizeof(*p->signal));
        
        return;
    }

    memmove(p->signal,
            p->signal + packetLength,
            shift * sizeof(*p->signal));
    
    memcpy(p->signal + shift,
           packet,
           packetLength * sizeof(*p->signal));
}

void processing_save_preview(Processing* p, const double* packet, size_t packetLength) {
    long int shift = p->previewLength - packetLength;
    
    if(shift <= 0) {
        memcpy(p->preview,
               packet - shift,
               p->previewLength * sizeof(*p->preview));
        
        return;
    }
    
    memmove(p->preview,
            p->preview + packetLength,
            shift * sizeof(*p->preview));
    
    memcpy(p->preview + shift,
           packet,
           packetLength * sizeof(*p->preview));
}

void processing_recalculate(Processing* p){
    memcpy(p->real, p->signal, p->signalLength * sizeof(*p->signal));
    memset(p->imag, 0, p->signalLength* sizeof(*p->signal));

    DSPDoubleSplitComplex spectrum = {p->real, p->imag};
    
    vDSP_fft_zipD(p->fs, &spectrum, 1, log2(p->signalLength), kFFTDirection_Forward);
    
    memset(p->spectrum, 0, p->signalLength * sizeof(*p->spectrum));
    
    vDSP_zaspecD(&spectrum, p->spectrum, p->signalLength);
    
    double peak = 0;
    double peakFrequency = 0;
    size_t peakIndex = 1;
    
    for(size_t i = 1; i < p->signalLength / 2; i ++){
        double spectrumValue = p->spectrum[i];
        double f = p->fd * i / p->signalLength;
        
        if (p->filter) {
            double f0 = p->targetFrequency * (double)p->targetHarmonic;
            
            double df = (f - f0) / f0;
            if(df < 0){
                df = 0;
            }
            spectrumValue *= exp(- 10.0 * df * df);
        }
        
        if (spectrumValue > peak) {
            peak = p->spectrum[i];
            peakIndex = i;
            peakFrequency = f;
        }
    }
    
    p->peakFrequency = peakFrequency;
    
    if (p->subcounter / 16) {
        data_append_and_shift(p->subSignalReal, p->subLength, p->real[peakIndex]);
        data_append_and_shift(p->subSignalImag, p->subLength, p->imag[peakIndex]);
        p->subcounter = 0;
    }else{
        p->subSignalReal[p->subLength-1] = p->real[peakIndex];
        p->subSignalImag[p->subLength-1] = p->imag[peakIndex];
    }
    
    p->subcounter++;
    
    double range = (double)p->fd / p->signalLength;
    p->peakSubFrequency = processing_clarify_peak_frequency_in_range(p, range);
}

double processing_get_frequency(Processing* p) {
    return p->peakFrequency;
}

double processing_get_sub_frequency(Processing* p) {
    return p->peakSubFrequency;
}

double processing_get_pulsation(Processing* p) {
    double peak_energy = 0;
    double energy = 0;
    size_t m = p->signalLength / 2;
    
    for(int i = 0; i < m; i++) {
        double ei = p->spectrum[i];
        if(ei > peak_energy) peak_energy = ei;
        energy += ei;
    }
    
    if(energy == 0) return 1.0;
    
    return (double)peak_energy / sqrt(energy);
}

int processing_get_harmonic_order(Processing* p) {
    const int maxOrder = 4;
    double eij[maxOrder] = {};
    
    double cutoff = 100; // Hz
    
    for(int i = 0; i < maxOrder; i++) {
        for(int j = 0; j < maxOrder; j++) {
            double fij = (double)(j + 1) / (double)(i + 1);
            double position = fij * p->signalLength * p->peakFrequency / p->fd;
            
            for(int k = -1; k <= 1; k ++) {
                if(k < 0) continue;
                
                double lowHarmonicGain = (p->peakFrequency < cutoff * maxOrder) && (j < i) ? 1 : 2;
                
                eij[i] += lowHarmonicGain * p->spectrum[(int)(position + k)];
            }
            
            //printf("%f ", p->spectrum[(int)position]);
        }
        
        //printf("\n");
    }
    
    //printf("\n");
    
    double e0 = 0;
    int order = 1;
    
    for(int i = 0; i < maxOrder; i++) {
        if(e0 >= eij[i]) continue;
        e0 = eij[i];
        order = i + 1;
    }
    
    //printf("order %i \n", order);
    //printf("pulsation %f\n", processing_get_pulsation(p));
    
    return order;
}

double processing_clarify_peak_frequency_in_range(Processing* p, double range) {
    memcpy(p->subSpectrumReal, p->subSignalReal, p->subLength * sizeof(*p->subSignalReal));
    memcpy(p->subSpectrumImag, p->subSignalImag, p->subLength * sizeof(*p->subSignalImag));
    
    DSPDoubleSplitComplex subspectrum = {p->subSpectrumReal, p->subSpectrumImag};
    vDSP_fft_zipD(p->sfs, &subspectrum, 1, log2(p->subLength), kFFTDirection_Forward);
    
    memset(p->subSpectrum, 0, p->subLength * sizeof(*p->subSpectrum));
    
    vDSP_zaspecD(&subspectrum, p->subSpectrum, p->subLength);
    
    double subPeak = 0;
    size_t subPeakIndex = 1;
    
    for(size_t i = 0; i < p->subLength; i ++) {
        if (p->subSpectrum[i] > subPeak) {
            subPeak = p->subSpectrum[i];
            subPeakIndex = i;
        }
    }
    
    if (subPeakIndex > p->subLength) {
        volatile double shift = subPeakIndex;
        return range * (shift - p->subLength) / p->subLength;
    }
    
    return range * subPeakIndex / p->subLength;
}

void processing_build_standing_wave2(Processing* p, double* wave, size_t length) {
    double f = p->peakFrequency;
    if(f < 40) f = 40;
    if(f > 16000) f = 16000;
    
    double waveLength = p->fd / f;
    
    size_t index = p->previewLength - waveLength * 2;
    
    double* src = &p->preview[index];
    
    double re = 0; double im = 0;
    for (size_t i = 0; i < waveLength*2; i++) {
        double t = (double)2.0 * M_PI * i / waveLength;
        re += src[i] * cos(t);
        im += src[i] * sin(t);
    }
    
    double phase = get_phase(re, im);
    
    double shift = waveLength * phase / (2.0 * M_PI);
    
    // here something wrong !!!
    // p->previewLength - waveLength * 2 - (size_t)(waveLength - shift) - (size_t)waveLength
    // p->previewLength - waveLength * 4 + (0 .. waveLength)
    double* shiftedSrc = &p->preview[index - (size_t)(waveLength - shift) - (size_t)waveLength];
    
    approximate_sinc(p->points, shiftedSrc, p->pointCount, 2*waveLength);
    double avr = data_avr(p->points, p->pointCount);
    data_shift(p->points, p->pointCount, -avr);
    double dev = data_dev(p->points, p->pointCount);
    if(dev != 0){
        data_scale(p->points, p->pointCount, 0.2/dev);
    }
    
    memcpy(wave, p->points, sizeof(*wave) * length);
}


double data_avr(double* data, size_t length) {
    double avr = 0;
    for (size_t i = 0; i < length; i++) {
        avr += data[i];
    }
    return avr / length;
}

void data_shift(double* data, size_t length, double shift) {
    for (size_t i = 0; i < length; i++) {
        data[i] += shift;
    }
}

void data_scale(double* data, size_t length, double scale){
    for (size_t i = 0; i < length; i++) {
        data[i] *= scale;
    }
}

double data_max(double* data, size_t length) {
    if (!length) {
        return NAN;
    }
    double maximum = data[0];
    for (size_t i = 1; i < length; i++) {
        if(data[i] > maximum) maximum = data[i];
    }
    return maximum;
}

double data_min(double* data, size_t length) {
    if (!length) {
        return NAN;
    }
    double minimum = data[0];
    for (size_t i = 1; i < length; i++) {
        if(data[i] < minimum) minimum = data[i];
    }
    return minimum;
}

double data_dev(double* data, size_t length) {
    return (data_max(data, length) - data_min(data, length)) / 2.0;
}

void data_append_and_shift(double* data, size_t length, double value){
    memmove(data, &data[1], (length-1) * sizeof(*data));
    data[length-1] = value;
}
