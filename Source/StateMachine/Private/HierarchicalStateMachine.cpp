// Fill out your copyright notice in the Description page of Project Settings.

#include "HierarchicalStateMachine.h"

#include <Engine/Engine.h>

#define STATEMACHINE_DEQUEUEEVENTS_DEFAULTLIMIT 5000

UHierarchicalStateMachine::Track::Track(FName _name, State* _parent, UHierarchicalStateMachine* _stateMachine)
	: m_name(_name)
	, m_parent(_parent)
	, m_stateMachine(_stateMachine)
{
}


UHierarchicalStateMachine::Track::~Track()
{
	for (auto& pair : m_states)
	{
		delete pair.Value;
	}
	m_states.Empty();
}


UHierarchicalStateMachine::State* UHierarchicalStateMachine::Track::AddState(FName _name, const StateEnterDelegate& _enter, const StateTickDelegate& _tick, const StateExitDelegate& _exit)
{
	State* state = AddState(_name);

	state->Enter = _enter;
	state->Tick = _tick;
	state->Exit = _exit;

	return state;
}


UHierarchicalStateMachine::State* UHierarchicalStateMachine::Track::AddState(FName _name)
{
	STATEMACHINE_ASSERTF(m_stateMachine->m_states.Find(_name) == nullptr, TEXT("A State with the name \"%s\" already exists."), *_name.GetPlainNameString());

	State* state = new State(_name, this, m_stateMachine);

	m_states.Add(_name) = state;
	m_stateMachine->m_states.Add(_name) = state;
	return state;
}

UHierarchicalStateMachine::State* UHierarchicalStateMachine::Track::AddDefaultState(FName _name, const StateEnterDelegate& _enter, const StateTickDelegate& _tick, const StateExitDelegate& _exit)
{
	STATEMACHINE_ASSERTF(m_defaultState == nullptr, TEXT("A State with the name \"%s\" already exists."), *_name.GetPlainNameString());

	State* state = AddState(_name, _enter, _tick, _exit);
	m_defaultState = state;
	return state;
}


UHierarchicalStateMachine::State* UHierarchicalStateMachine::Track::AddDefaultState(FName _name)
{
	State* state = AddState(_name);
	m_defaultState = state;
	return state;
}

UHierarchicalStateMachine::State::State(FName _name, Track* _parent, UHierarchicalStateMachine* _stateMachine)
	: m_name(_name)
	, m_parent(_parent)
	, m_stateMachine(_stateMachine)
{
}


UHierarchicalStateMachine::State::~State()
{
	for (auto& pair : m_tracks)
	{
		delete pair.Value;
	}
	m_tracks.Empty();
}


UHierarchicalStateMachine::Track* UHierarchicalStateMachine::State::AddTrack(FName _name)
{
	STATEMACHINE_ASSERTF(m_stateMachine->m_tracks.Find(_name) == nullptr, TEXT("A Track with the name \"%s\" already exists."), *_name.GetPlainNameString());

	Track* track = new Track(_name, this, m_stateMachine);
	m_tracks.Add(_name) = track;
	m_stateMachine->m_tracks.Add(_name) = track;
	return track;
}


bool UHierarchicalStateMachine::State::IsInTrack(const Track* _track)
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


UHierarchicalStateMachine::UHierarchicalStateMachine()
	: bImmediatelyDequeueEvents(true)
{
}


UHierarchicalStateMachine::~UHierarchicalStateMachine()
{
	for (auto& pair : m_eventTransitions)
	{
		for (EventTransition* transition : pair.Value)
		{
			delete transition;
		}
	}
	m_eventTransitions.Empty();

	for (Track* track : m_rootTracks)
	{
		delete track;
	}
	m_rootTracks.Empty();

	m_tracks.Empty();
	m_states.Empty();
}


UHierarchicalStateMachine::Track* UHierarchicalStateMachine::AddRootTrack(FName _name)
{
	Track* track = new Track(_name, nullptr, this);
	return AddRootTrack(track);
}

UHierarchicalStateMachine::Track * UHierarchicalStateMachine::AddRootTrack(Track * _track)
{
#if DO_CHECK
	_VisitTrack(_track, TrackVisitorDelegate::CreateUObject(this, &UHierarchicalStateMachine::_AssertIfTrackExists), StateVisitorDelegate::CreateUObject(this, &UHierarchicalStateMachine::_AssertIfStateExists));
#endif

	m_rootTracks.Add(_track);
	m_tracks.Add(_track->m_name) = _track;
	return _track;
}


void UHierarchicalStateMachine::AddEventTransition(FName _eventName, FName _sourceStateName, FName _targetStateName)
{
	State* sourceState = *m_states.Find(_sourceStateName);
	State* targetState = *m_states.Find(_targetStateName);
	EventTransition* eventTransition = new EventTransition();
	eventTransition->name = _eventName;
	eventTransition->source = sourceState;
	eventTransition->target = targetState;
	m_eventTransitions.FindOrAdd(_eventName).Add(eventTransition);
}


void UHierarchicalStateMachine::Start()
{
	STATEMACHINE_ASSERT(!IsStarted());
	STATEMACHINE_ASSERT(m_currentStates.Num() == 0);

#if STATEMACHINE_HISTORY_ENABLED
	_LogStateMachineStarted();
#endif

	TArray<Track*> waitingTracks;
	for (Track* track : m_rootTracks)
	{
		waitingTracks.Push(track);
	}

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
		state->Enter.ExecuteIfBound();
#if STATEMACHINE_HISTORY_ENABLED 
		_LogStateEntered(state);
#endif
	}

	m_started = true;
}


void UHierarchicalStateMachine::Tick(float _dt)
{
	STATEMACHINE_ASSERT(IsStarted());
	STATEMACHINE_ASSERT(!m_ticking);

	DequeueEvents();

	m_ticking = true;
	for (State* state : m_currentStates)
	{
		state->Tick.ExecuteIfBound(_dt);
	}
	m_ticking = false;

	DequeueEvents();

	if (!m_started)
	{
		Stop();
	}
}


void UHierarchicalStateMachine::Stop()
{
	STATEMACHINE_ASSERT(IsStarted());
	m_started = false;
	if (!m_ticking)
	{
		for (int i = m_currentStates.Num() - 1; i >= 0; --i)
		{
			m_currentStates[i]->Exit.ExecuteIfBound();
#if STATEMACHINE_HISTORY_ENABLED 
			_LogStateExited(m_currentStates[i]);
#endif
		}
		m_currentStates.Empty();
	}

#if STATEMACHINE_HISTORY_ENABLED
	_LogStateMachineStopped();
#endif
}


void UHierarchicalStateMachine::PostEvent(FName _eventName)
{
	STATEMACHINE_ASSERTF(m_eventTransitions.Find(_eventName) != nullptr, TEXT("Unknown event name \"%s\"."), *_eventName.GetPlainNameString());

	m_eventsQueue.Add(_eventName);
#if STATEMACHINE_HISTORY_ENABLED 
	_LogEventPushed(_eventName);
#endif
	if (bImmediatelyDequeueEvents && !m_ticking && IsStarted())
	{
		DequeueEvents();
	}
}


void UHierarchicalStateMachine::DebugDisplayCurrentStates(const FColor& _color)
{
	if (GEngine)
	{
		FString states;
		for (State* state : m_currentStates)
		{
			states += state->GetParentTrack()->GetName().GetPlainNameString();
			states += ": ";
			states += state->GetName().GetPlainNameString();
			states += "\n";
		}

		GEngine->AddOnScreenDebugMessage(-1, 0.f, _color, *states);
	}
}

void UHierarchicalStateMachine::SerializeCurrentStates(TArray<FString>& _outStates)
{
	STATEMACHINE_ASSERT(!m_ticking);

	for (State* state : m_currentStates)
	{
		_outStates.Add(state->GetName().ToString());
	}
}

void UHierarchicalStateMachine::DeserializeCurrentStates(const TArray<FString>& _states)
{
	STATEMACHINE_ASSERT(!m_ticking);

	TArray<State*> states;
	for (const FString& state : _states)
	{
		State** statePtr = m_states.Find(FName(*state));
		if (!statePtr)
		{
			UE_LOG(LogTemp, Error, TEXT("Deserializing unknown State, aborting."));
			return;
		}
		states.Add(*statePtr);
	}

	for (int i = m_currentStates.Num() - 1; i >= 0; --i)
	{
		m_currentStates[i]->Exit.ExecuteIfBound();

#if STATEMACHINE_HISTORY_ENABLED 
		_LogStateExited(m_currentStates[i]);
#endif
	}

	m_currentStates = states;
	for (auto state : m_currentStates)
	{
		state->Enter.ExecuteIfBound();
#if STATEMACHINE_HISTORY_ENABLED 
		_LogStateEntered(state);
#endif
	}
}

bool UHierarchicalStateMachine::_VisitTrack(Track* _track, TrackVisitorDelegate _trackVisitor, StateVisitorDelegate _stateVisitor)
{
	if (!_trackVisitor.Execute(_track))
	{
		return false;
	}

	for (auto& pair : _track->m_states)
	{
		if (!_VisitState(pair.Value, _trackVisitor, _stateVisitor))
		{
			return false;
		}
	}
	return true;
}


bool UHierarchicalStateMachine::_VisitState(State* _state, TrackVisitorDelegate _trackVisitor, StateVisitorDelegate _stateVisitor)
{
	if (!_stateVisitor.Execute(_state))
	{
		return false;
	}

	for (auto& pair : _state->m_tracks)
	{
		if (!_VisitTrack(pair.Value, _trackVisitor, _stateVisitor))
		{
			return false;
		}
	}
	return true;
}

bool UHierarchicalStateMachine::_AssertIfTrackExists(Track* _track)
{
	STATEMACHINE_ASSERTF(m_tracks.Find(_track->m_name) == nullptr, TEXT("A Track with the name \"%s\" already exists."), *_track->m_name.GetPlainNameString());
	return true;
}

bool UHierarchicalStateMachine::_AssertIfStateExists(State * _state)
{
	STATEMACHINE_ASSERTF(m_states.Find(_state->m_name) == nullptr, TEXT("A State with the name \"%s\" already exists."), *_state->m_name.GetPlainNameString());
	return true;
}


UHierarchicalStateMachine::Track* UHierarchicalStateMachine::_FindClosestCommonTrack(const State* _stateA, const State* _stateB)
{
	if (_stateA->m_stateMachine != _stateB->m_stateMachine)
		return nullptr;

	if (_stateA->m_parent == _stateB->m_parent)
		return _stateA->m_parent; // Easy skip

	TArray<Track*> ATracks;
	for (auto& pair : _stateA->m_tracks)
	{
		ATracks.Add(pair.Value);
	}
	Track* currentTrack = _stateA->m_parent;
	while (currentTrack != nullptr)
	{
		ATracks.Add(currentTrack);
		currentTrack = currentTrack->m_parent ? currentTrack->m_parent->m_parent : nullptr;
	}

	for (auto& pair : _stateB->m_tracks)
	{
		int32 i = ATracks.Find(pair.Value);
		if (i != INDEX_NONE)
		{
			return ATracks[i];
		}
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

	return nullptr;
}

void UHierarchicalStateMachine::DequeueEvents(uint16 _dequeuedEventsLimit)
{
	if (_dequeuedEventsLimit == -1)
		_dequeuedEventsLimit = STATEMACHINE_DEQUEUEEVENTS_DEFAULTLIMIT;

	uint16 dequeuedEventsCount = 0;
	while ((dequeuedEventsCount < _dequeuedEventsLimit) && m_eventsQueue.Num() != 0)
	{
		++dequeuedEventsCount;
		FName evt = m_eventsQueue[0];
		m_eventsQueue.RemoveAt(0);
#if STATEMACHINE_HISTORY_ENABLED
		_LogEventPopped(evt);
#endif
		const TArray<EventTransition*>& transitions = *m_eventTransitions.Find(evt);

		// OPTIM: could be member arrays in order to limit allocations
		TArray<State*> pathFromCommonTrackToState;
		TArray<Track*> tracksToSet;

		for (const EventTransition* transition : transitions)
		{
			if (m_currentStates.Find(transition->source) == INDEX_NONE)
			{
				continue;
			}

			pathFromCommonTrackToState.Empty();
			tracksToSet.Empty();

			// OPTIM: don't need the two loops
			Track* commonTrack = _FindClosestCommonTrack(transition->source, transition->target);

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
					m_currentStates[i]->Exit.ExecuteIfBound();
#if STATEMACHINE_HISTORY_ENABLED 
					_LogStateExited(m_currentStates[i]);
#endif
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

				currentState->Enter.ExecuteIfBound();
#if STATEMACHINE_HISTORY_ENABLED 
				_LogStateEntered(currentState);
#endif
				m_currentStates.Add(currentState);

				for (auto& pair : currentState->m_tracks)
				{
					tracksToSet.Add(pair.Value);
				}
			}
		}
	}

	if (dequeuedEventsCount >= STATEMACHINE_DEQUEUEEVENTS_DEFAULTLIMIT)
	{
		UE_LOG(LogTemp, Error, TEXT("[StateMachine] Stopped events dequeuing after having dequeued more than %d events. There may be an infinite events loop somewhere."), STATEMACHINE_DEQUEUEEVENTS_DEFAULTLIMIT);
	}
}

#if STATEMACHINE_HISTORY_ENABLED 

void UHierarchicalStateMachine::_LogStateMachineStarted()
{
	HistoryEntry entry;
	entry.type = HistoryEntryType_StateMachineStarted;
	entry.time = FDateTime::Now();
	m_history.Add(entry);

#if PRINT_HISTORY_IN_LOG
	UE_LOG(LogTemp, Display, TEXT("[StateMachine] Started State Machine."));
#endif
}

void UHierarchicalStateMachine::_LogStateMachineStopped()
{
	HistoryEntry entry;
	entry.type = HistoryEntryType_StateMachineStopped;
	entry.time = FDateTime::Now();
	m_history.Add(entry);

#if PRINT_HISTORY_IN_LOG
	UE_LOG(LogTemp, Display, TEXT("[StateMachine] Stopped State Machine."));
#endif
}

void UHierarchicalStateMachine::_LogStateEntered(State* _state)
{
	HistoryEntry entry;
	entry.type = HistoryEntryType_StateEntered;
	entry.time = FDateTime::Now();
	entry.state = _state;
	m_history.Add(entry);

#if PRINT_HISTORY_IN_LOG
	UE_LOG(LogTemp, Display, TEXT("[StateMachine] Entered state \"%s\"."), *_state->m_name.GetPlainNameString());
#endif
}

void UHierarchicalStateMachine::_LogStateExited(State* _state)
{
	HistoryEntry entry;
	entry.type = HistoryEntryType_StateExited;
	entry.time = FDateTime::Now();
	entry.state = _state;
	m_history.Add(entry);

#if PRINT_HISTORY_IN_LOG
	UE_LOG(LogTemp, Display, TEXT("[StateMachine] Exited state \"%s\"."), *_state->m_name.GetPlainNameString());
#endif
}

void UHierarchicalStateMachine::_LogEventPushed(FName _name)
{
	HistoryEntry entry;
	entry.type = HistoryEntryType_EventPushed;
	entry.time = FDateTime::Now();
	entry.eventName = _name;
	m_history.Add(entry);

#if PRINT_HISTORY_IN_LOG
	UE_LOG(LogTemp, Display, TEXT("[StateMachine] Pushed event \"%s\"."), *_name.GetPlainNameString());
#endif
}

void UHierarchicalStateMachine::_LogEventPopped(FName _name)
{
	HistoryEntry entry;
	entry.type = HistoryEntryType_EventPopped;
	entry.time = FDateTime::Now();
	entry.eventName = _name;
	m_history.Add(entry);

#if PRINT_HISTORY_IN_LOG
	UE_LOG(LogTemp, Display, TEXT("[StateMachine] Popped event \"%s\"."), *_name.GetPlainNameString());
#endif
}
#endif
