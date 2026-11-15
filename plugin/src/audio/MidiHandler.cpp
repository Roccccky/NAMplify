#include "MidiHandler.h"
#include "../service/Settings.h"
#include <namplify/engine/Params.h>
#include <vector>

namespace nmpl
{

MidiHandler::MidiHandler (Settings& s) : settings (s) {}

void MidiHandler::rebuildIfNeeded()
{
    if (! tableDirty.exchange (false)) return;

    table.clear();
    for (const auto& a : settings.assignments())
    {
        const bool isSwitch = a.paramIndex >= 0 && a.paramIndex < nmp::kNumParams
                           && nmp::paramDesc (a.paramIndex).kind == nmp::ParamKind::boolean;
        table.push_back ({ a.ccNumber, a.channel, a.paramIndex, isSwitch, {} });
    }
}

void MidiHandler::push (const Event& e)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) events[scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) events[scope.startIndex2] = e;
}

std::vector<MidiHandler::Event> MidiHandler::takeEvents()
{
    std::vector<Event> out;
    const auto scope = fifo.read (fifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i) out.push_back (events[scope.startIndex1 + i]);
    for (int i = 0; i < scope.blockSize2; ++i) out.push_back (events[scope.startIndex2 + i]);

    return out;
}

void MidiHandler::processBlock (juce::MidiBuffer& incoming, juce::MidiBuffer& outgoing,
                                nmp::Transport& transport)
{
    const int wantChannel = channel.load (std::memory_order_relaxed);
    const bool doThru = thru.load (std::memory_order_relaxed);
    const bool doPc   = programChange.load (std::memory_order_relaxed);

    for (const auto meta : incoming)
    {
        const auto message = meta.getMessage();

        // Clock und Transport ohne Kanalfilter, sie haben keinen Kanal.
        if (message.isMidiClock())        { transport.midiClockPulse(); }
        else if (message.isMidiStart())   { transport.midiStart(); }
        else if (message.isMidiContinue()){ transport.midiContinue(); }
        else if (message.isMidiStop())    { transport.midiStop(); }

        const bool channelMatches = wantChannel == 0
                                 || (message.getChannel() == wantChannel);

        if (channelMatches && message.isController())
        {
            const int cc = message.getControllerNumber();
            const int ch = message.getChannel();
            const float value = (float) message.getControllerValue() / 127.0f;

            const int learning = learnTarget.load (std::memory_order_relaxed);
            if (learning >= 0)
            {
                Event e;
                e.type = Event::Type::learned;
                e.paramIndex = learning;
                e.cc = cc;
                e.ccChannel = wantChannel;
                push (e);
                learnTarget.store (-1, std::memory_order_relaxed);
            }
            else
            {
                for (auto& m : table)
                {
                    if (m.cc != cc) continue;
                    if (m.channel != 0 && m.channel != ch) continue;

                    Event e;
                    e.paramIndex = m.param;

                    if (m.isSwitch)
                    {
                        const auto action = m.press.feed (message.getControllerValue(),
                                                          juce::Time::getMillisecondCounterHiRes());
                        if (action == nmp::SwitchPress::Action::none) break;
                        e.type = action == nmp::SwitchPress::Action::toggle ? Event::Type::toggle
                                                                            : Event::Type::parameter;
                        e.unitValue = 0.0f;
                    }
                    else
                    {
                        e.type = Event::Type::parameter;
                        e.unitValue = value;
                    }

                    push (e);
                    break;      // ein Regler pro CC
                }
            }
        }

        if (doPc && channelMatches && message.isProgramChange())
        {
            Event e;
            e.type = Event::Type::programChange;
            e.program = message.getProgramChangeNumber();
            push (e);
        }

        if (doThru) outgoing.addEvent (message, meta.samplePosition);
    }
}

} // namespace nmpl
