/* -*- mode:c++ -*- ********************************************************
 * file:        UWBIRRadio.h
 *
 * author:      Jerome Rousselot <jerome.rousselot@csem.ch>
 *
 * copyright:   (C) 2008 Centre Suisse d'Electronique et Microtechnique (CSEM) SA
 * 				Systems Engineering
 *              Real-Time Software and Networking
 *              Jaquet-Droz 1, CH-2002 Neuchatel, Switzerland.
 *
 *              This program is free software; you can redistribute it
 *              and/or modify it under the terms of the GNU General Public
 *              License as published by the Free Software Foundation; either
 *              version 2 of the License, or (at your option) any later
 *              version.
 *              For further information see file COPYING
 *              in the top level directory
 * description: this Radio subclass computes the power consumption of the radio
 * 				and also adds a synchronization state.
 ***************************************************************************/

#ifndef UWBIRRADIO_H_
#define UWBIRRADIO_H_

#include "PhyUtils.h"
#include "BasePhyLayer.h"
#include "UWBIRIEEE802154APathlossModel.h"

/**
 * @brief This class extends the basic radio model.
 * It monitors the radio power consumption, and adds a SYNC state before reception.
 * The decider tells the uwb phy layer when it locks on a frame, and the uwb phy layer
 * then sets the uwb radio state into RX mode.
 * This is done through a private method so that the MAC can not change these states.
 * This is why this class is friend with uwbirphylayer.
 *
 */

class UWBIRRadio: public Radio {
	friend class UWBIRPhyLayer;

public:

	enum UWBIRRadioState {
		/* receiving state*/
		RX = 0,
		/* transmiting state*/
		TX,
		/* sleeping*/
		SLEEP,
		/* switching between two states*/
		SWITCHING,
		/* state added for synchronization */
		SYNC,
		/*
		 * NOTE: THIS IS NO REAL RADIOSTATE, JUST A COUNTER FOR THE NUMBER OF RADIOSTATES
		 * IT ALWAYS NEEDS TO BE THE LAST ENTRY IN THIS ENUM!
		 *
		 * Sub-classing Radios which want to add more states can add their own
		 * states beginning at the value of NUM_RADIO_STATES.
		 * They should also remember to update the "numRadioStates" member accordingly.
		 */
		NUM_RADIO_STATES
	};

protected:
	/** @brief Stores the power consumption of each radio state (in mW) */
	double* powerConsumptions;
	simtime_t powerConsumption;
	simtime_t lastStateChange;

	cOutVector vectorPower;

public:

	void setName(string name) {
		vectorPower.setName(name.c_str());  // so that we have a name that include our address / node number
	}

	/** @brief Defines the power consumption in the selected radio state. */
	virtual void setPowerConsumption(int radioState, double _powerConsumption) {
		assert(0 <= radioState);
		assert(radioState < numRadioStates);
		powerConsumptions[radioState] = _powerConsumption;
	}

	virtual ~UWBIRRadio() {
		delete[] powerConsumptions;
		powerConsumptions = 0;
	}

	/* Static factory method (see Radio class in PhyUtils.h) */
	static UWBIRRadio* createNewUWBIRRadio(int initialState = SYNC,
								 double minAtt = 1.0,
								 double maxAtt = 0.0)
	{
		return new UWBIRRadio(UWBIRRadio::NUM_RADIO_STATES,
						 initialState,
						 minAtt, maxAtt);
	}


	/**
	 * @brief This switchTo method only accepts three states to switch to:
	 * reception, transmission and sleep.
	 */

	virtual simtime_t switchTo(int newState, simtime_t now) {
		// state must be one of sleep, receive or transmit (not sync)
		//assert(newState != Radio::SYNC);
		if(newState == state || (newState == UWBIRRadio::RX && state == UWBIRRadio::SYNC)) {
			return -1; // nothing to do
		} else {
			if(newState == UWBIRRadio::RX) {
				// prevent entering "frame reception" immediately
				newState = UWBIRRadio::SYNC;
			}
			return reallySwitchTo(newState, now);
		}
	}

	virtual simtime_t reallySwitchTo(int newState, simtime_t now) {
		updatePowerConsumption(now);
		// set the nextState to the newState and the current state to SWITCHING
		nextState = newState;
		int lastState = state;
		state = UWBIRRadio::SWITCHING;
		radioStates.record(state);
		// make entry to RSAM
		makeRSAMEntry(now, state);

		// return matching entry from the switch times matrix
		return swTimes[lastState][nextState];
	}

	virtual void endSwitch(simtime_t now) {
		updatePowerConsumption(now);
		Radio::endSwitch(now);
	}

	/** @brief Getter function to read the power consumption of this radio
	 * from simulation start until now. */
	simtime_t getPowerConsumption(simtime_t now) {
		return powerConsumption + (now - lastStateChange) * powerConsumptions[state];
	}

protected:

	UWBIRRadio(int numRadioStates, int initialState = SYNC, double minAtt = 1.0, double maxAtt = 0.0)
	:Radio(numRadioStates, initialState, minAtt, maxAtt) {

		lastStateChange = 0;
		powerConsumption = 0;
		powerConsumptions = new double [numRadioStates];

		for (int i = 0; i < numRadioStates; i++)
		{
			// initialize all power consumption entries to 0.0
			powerConsumptions[i] = 0.0;
		}
	}

	virtual double mapStateToAtt(int state)
	{
		if (state == UWBIRRadio::RX || state == UWBIRRadio::SYNC)
		{
			return minAtt;
		} else
		{
			return maxAtt;
		}
	}

private:
	/**
	 * @brief Called by the decider through the phy layer to announce that
	 * the radio has locked on a frame and is attempting reception.
	 */
	virtual void startReceivingFrame(simtime_t now) {
		assert(state == UWBIRRadio::SYNC);
		state = UWBIRRadio::SWITCHING;
		nextState = UWBIRRadio::RX;
		endSwitch(now);
	}
	/**
		 * @brief Called by the decider through the phy layer to announce that
		 * the radio has finished receiving a frame and is attempting to
		 * synchronize on incoming frames.
		 */
	virtual void finishReceivingFrame(simtime_t now) {
		assert(state == UWBIRRadio::RX);
		state = UWBIRRadio::SWITCHING;
		nextState = UWBIRRadio::SYNC;
		endSwitch(now);
	}

	/**
	 * @brief Updates the power consumption counter at each state switch.
	 */
	virtual void updatePowerConsumption(simtime_t now) {
		assert(now >= lastStateChange);
		simtime_t delta = (now - lastStateChange) * powerConsumptions[state];
		powerConsumption = powerConsumption + delta;
		lastStateChange = now;
		vectorPower.record(powerConsumption / simTime().dbl());
	}
};

#endif /* UWBIRRADIO_H_ */
