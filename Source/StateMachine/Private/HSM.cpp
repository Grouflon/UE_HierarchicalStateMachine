// Fill out your copyright notice in the Description page of Project Settings.

#include "HSM.h"


HSM::Track::Track(FName _name, State* _parent, HSM* _hsm)
	: m_name(_name)
	, m_parent(_parent)
	, m_hsm(_hsm)
{
}


HSM::Track::~Track()
{
	for (auto& pair : m_states)
	{
		delete pair.Value;
	}
	m_states.Empty();
}


HSM::State* HSM::Track::AddState(FName _name, const StateBeginDelegate& _begin, const StateUpdateDelegate& _update, const StateEndDelegate& _end)
{
	check(m_hsm->m_states.Find(_name) == nullptr);

	State* state = new State(_name, this, m_hsm);
	state->Begin = _begin;
	state->Update = _update;
	state->End = _end;

	m_states.Add(_name) = state;
	m_hsm->m_states.Add(_name) = state;
	return state;
}


HSM::State* HSM::Track::AddDefaultState(FName _name, const StateBeginDelegate& _begin, const StateUpdateDelegate& _update, const StateEndDelegate& _end)
{
	check(m_defaultState == nullptr);

	State* state = AddState(_name, _begin, _update, _end);
	m_defaultState = state;
	return state;
}


HSM::State::State(FName _name, Track* _parent, HSM* _hsm)
	: m_name(_name)
	, m_parent(_parent)
	, m_hsm(_hsm)
{
}


HSM::State::~State()
{
	for (auto& pair : m_tracks)
	{
		delete pair.Value;
	}
	m_tracks.Empty();
}


HSM::Track* HSM::State::AddTrack(FName _name)
{
	check(m_hsm->m_tracks.Find(_name) == nullptr);

	Track* track = new Track(_name, this, m_hsm);
	m_tracks.Add(_name) = track;
	m_hsm->m_tracks.Add(_name) = track;
	return track;
}


bool HSM::State::IsInTrack(const Track* _track)
{
	Track* currentTrack = m_parent;
	while (currentTrack != nullptr)
	{
		if (currentTrack == _track)
			return true;

		currentTrack = currentTrack->m_parent ? currentTrack->m_parent->m_parent : nullptr;
	}
	return false;
}


HSM::HSM()
	: m_root("root", nullptr, this)
{
}


HSM::~HSM()
{
	for (auto& pair : m_eventTransitions)
	{
		for (EventTransition* transition : pair.Value)
		{
			delete transition;
		}
	}
	m_eventTransitions.Empty();

	for (PollingTransition* transition : m_pollingTransitions)
	{
		delete transition;
	}
	m_pollingTransitions.Empty();

	// NOTE: Should HSM be responsible of all states and tracks instead of counting on the hierarchy ?
	// Seems pretty equivalent
}


void HSM::AddEventTransition(FName _sourceStateName, FName _targetStateName, FName _eventName)
{
	State* sourceState = *m_states.Find(_sourceStateName);
	State* targetState = *m_states.Find(_targetStateName);
	EventTransition* eventTransition = new EventTransition();
	eventTransition->source = sourceState;
	eventTransition->target = targetState;
	m_eventTransitions.FindOrAdd(_eventName).Add(eventTransition);
}


void HSM::AddPollingTransition(FName _sourceStateName, FName _targetStateName, const PollingTransitionDelegate& _pollingDelegate, bool _negate)
{
	State* sourceState = *m_states.Find(_sourceStateName);
	State* targetState = *m_states.Find(_targetStateName);
	PollingTransition* pollingTransition = new PollingTransition();
	pollingTransition->source = sourceState;
	pollingTransition->target = targetState;
	pollingTransition->pollingDelegate = _pollingDelegate;
	pollingTransition->negate = _negate;
	m_pollingTransitions.Add(pollingTransition);
}


HSM::Track* HSM::GetRootTrack()
{
	return &m_root;
}


void HSM::Start()
{
	check(m_currentStates.Num() == 0);

	TArray<Track*> waitingTracks;
	waitingTracks.Push(&m_root);
	while (waitingTracks.Num() != 0)
	{
		Track* track = waitingTracks.Pop();
		m_currentStates.Add(track->m_defaultState);
		for (auto& pair : track->m_defaultState->m_tracks)
		{
			waitingTracks.Push(pair.Value);
		}
	}

	for (State* state : m_currentStates)
	{
		state->Begin.ExecuteIfBound();
	}

	m_started = true;
}


void HSM::Update(float _dt)
{
	check(m_started);
	check(!m_updating);

	m_updating = true;
	for (State* state : m_currentStates)
	{
		state->Update.ExecuteIfBound(_dt);
	}
	m_updating = false;

	UnqueueEvents();

	if (!m_started)
	{
		Stop();
	}
}


void HSM::Stop()
{
	m_started = false;
	if (!m_updating)
	{
		for (int i = m_currentStates.Num() - 1; i >= 0; --i)
		{
			m_currentStates[i]->End.ExecuteIfBound();
		}
		m_currentStates.Empty();
	}
}


void HSM::PostEvent(FName _eventName)
{
	check(m_eventTransitions.Find(_eventName) != nullptr);

	m_eventsQueue.Add(_eventName);
	if (!m_updating)
	{
		UnqueueEvents();
	}
}


void HSM::UnqueueEvents()
{
	while (m_eventsQueue.Num() != 0)
	{
		FName evt = m_eventsQueue.Pop();
		const TArray<EventTransition*>& transitions = *m_eventTransitions.Find(evt);

		// OPTIM: could be member arrays in order to limit allocations
		TArray<State*> pathFromCommonTrackToState;
		TArray<Track*> tracksToSet;

		for (const EventTransition* transition : transitions)
		{
			if (m_currentStates.Find(transition->source) == INDEX_NONE ||
				m_currentStates.Find(transition->target) != INDEX_NONE)
			{
				continue;
			}
			
			pathFromCommonTrackToState.Empty();
			tracksToSet.Empty();

			// OPTIM: don't need the two loops
			Track* commonTrack = FindClosestCommonTrack(transition->source, transition->target);

			// find path from common track to target state
			pathFromCommonTrackToState.Insert(transition->target, 0);
			{
				Track* currentTrack = transition->target->m_parent;
				while (currentTrack != commonTrack)
				{
					pathFromCommonTrackToState.Insert(currentTrack->m_parent, 0);
					currentTrack = currentTrack->m_parent->m_parent;
				}
			}

			// End exiting tracks
			for (int i = m_currentStates.Num() - 1; i >= 0; --i)
			{
				if (m_currentStates[i]->IsInTrack(commonTrack))
				{
					m_currentStates[i]->End.ExecuteIfBound();
					m_currentStates.RemoveAt(i);
				}
			}

			// Go down new state tree
			tracksToSet.Add(commonTrack);
			while (tracksToSet.Num() != 0)
			{
				Track* currentTrack = tracksToSet.Pop();
				State* currentState = currentTrack->m_defaultState;

				if (pathFromCommonTrackToState.Num() != 0)
				{
					State** result = currentTrack->m_states.Find(pathFromCommonTrackToState[0]->m_name);
					if (result)
					{
						currentState = *result;
						pathFromCommonTrackToState.RemoveAt(0);
					}
				}

				currentState->Begin.ExecuteIfBound();
				m_currentStates.Add(currentState);

				for (auto& pair : currentState->m_tracks)
				{
					tracksToSet.Add(pair.Value);
				}
			}
		}
	}
}

void HSM::UpdatePollingEvents()
{

}

HSM::Track* HSM::FindClosestCommonTrack(const State* _stateA, const State* _stateB)
{
	if (_stateA->m_parent == _stateB->m_parent)
		return _stateA->m_parent; // Easy skip

	TArray<Track*> ATracks;
	Track* currentTrack = _stateA->m_parent;
	while (currentTrack != nullptr)
	{
		ATracks.Add(currentTrack);
		currentTrack = currentTrack->m_parent ? currentTrack->m_parent->m_parent : nullptr;
	}

	currentTrack = _stateB->m_parent;
	while (currentTrack != nullptr)
	{
		int32 i = ATracks.Find(currentTrack);
		if (i != INDEX_NONE)
		{
			return ATracks[i];
		}
		currentTrack = currentTrack->m_parent->m_parent;
	}
	checkNoEntry();
	return nullptr;
}
