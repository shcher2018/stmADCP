# Custom Nortek Signature ADCP Synchronization Controller a.k.a. “Trigger Board”

## Overview
This project implements a microcontroller-based scheduler for synchronizing
multiple instruments with deterministic timing and configurable phase offsets.

## Introduction
It is often desirable to mount several ADCPs on a platform – e.g., to provide upward/downward beam orientation. 
In these cases, the ADCPs need to be synchronized to avoid cross-talk contamination of observations. 
Nortek Signature ADCPs provide means to both send and receive synchronization pulses (TTL or RS485) or serial synchronization commands. There are several important limitations:
Nortek synchronization scheme makes the ADCPs ping synchronously. 
This avoids transmit/receive interference (which is important), but not necessarily interference of reflected pings. 
Nortek scheme only works when ping schedule is exactly the same for both ADCPs, in that the same type of pings (broadband [LR] vs. pulse-coherent [HR] vs. echosounder) are issued at the same time. The reason for this is that HR/LR pings have very different transmit pulse length, so we get transmit/receive interference even when the pings start at the same time. It is expected that identical setup of the two ADCPs would be enough to ensure that the ping schedule remains uniform. However, this is not the case: If one of the ADCPs encounters an interruption, the schedules can “slide” relative to each other. 
Ping-to-ping delays are hard-wired for a particular sampling scheme. There appears to be a desire to “cluster” the pings in time, so minimal intervals as short as 1/16th of a second are common. Such short intervals lead to previous-ping interference at certain distances from the surface/bottom (around 1/2  1/16 c≈ 47m). 
These limitations are inherent in Nortek’s approach to synchronization and ping scheduling. They cannot be fully mitigated by altering the instrument’s setup.
An alternative solution is to provide the ADCPs with independent trigger pulses, managed by a dedicated microcontroller (MCU). 

In the “ideal” synchronization scheme, we would want to 

1. 	Allow independent triggering of the ADCPs. This would allow full deconflicting of the pings.
2. Make the ping schedule(s) more flexible, allowing the two ADCPs to have different number of pings during each one-second interval.
3. Allow more flexibility in ping intervals to avoid previous-ping interference.
4. Ensure that two ADCPs start sampling at the same time regardless of when they were initialized. This would facilitate synchronization of ADCP clocks.
5. Open the possibility for synchronization of more than two ADCPs or synchronization with other sonars.

To achieve this and provide synchronization of two Nortek Signature1000 ADCPs, a STM32-based “Trigger Board” was developed for NSF Anisotropy project. 


## Full description
[Custom Nortek Signature ADCP Synchronization Controller](https://doi.org/10.5281/zenodo.19861825)

## Build
1. Open `stmADCP.ioc` in STM32CubeIDE
2. Generate code
3. Build and flash

## Citation
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19861825.svg)](https://doi.org/10.5281/zenodo.19861825)
