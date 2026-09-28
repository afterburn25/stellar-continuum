using System;
using System.Collections.Generic;
using System.Linq;
using Game.Presentation.Audio.Voice;
using Game.Simulation.Models;
using Godot;

namespace Game.Presentation;

public partial class Main
{
    public IReadOnlyList<string> UiVoiceEventKeys => _voiceEvents?.EventKeys ?? Array.Empty<string>();
    private VoiceEventRouter CreateGameplayVoiceRouter()
    {
        var profiles = new VoiceProfileRegistry(UiVoice!.Profiles);
        var speakers = CharacterVoiceResolver.FromJson(profiles,
            Godot.FileAccess.GetFileAsString("res://data/voice_profiles/roles.json"), ResolveCurrentVoiceCharacter);
        return VoiceEventRouter.FromJson(Godot.FileAccess.GetFileAsString("res://data/voice_profiles/events.json"), UiVoice.Speak, speakers);
    }

    private VoiceCharacter? ResolveCurrentVoiceCharacter(VoiceSpeakerContext context)
    {
        var civilization = _galaxy.Civilizations.FirstOrDefault(c => c.Id == context.SourceCivilizationId);
        if (civilization is null) return null;
        // Only current publicly presented offices may identify foreign speakers. Their internal
        // assignments are never consulted by the receiving player's presentation.
        if (civilization.Id != _galaxy.PlayerCivilizationId) return null;
        var office = context.Role switch
        {
            VoiceSpeakerRole.AlienDiplomat => "Diplomat",
            VoiceSpeakerRole.AlienScientist => "ChiefScientist",
            VoiceSpeakerRole.AlienCommander => "FleetCommander",
            _ => context.Role.ToString(),
        };
        var character = string.IsNullOrWhiteSpace(context.ExactCharacterId)
            ? civilization.Leadership.Current(office) : civilization.Leadership.Find(context.ExactCharacterId);
        return character is null ? null : new VoiceCharacter(character.Id, character.DisplayName, office,
            character.VoiceProfileId ?? "", character.Portrait);
    }

    public bool UiAssignVoiceCharacter(VoiceSpeakerRole role, string profileId, string name)
    {
        if (!UiIsDeveloperMode || UiVoice is null) return false;
        var profile = UiVoice.Profiles.FirstOrDefault(p => p.Id == profileId && p.Enabled);
        var civilization = _galaxy.Civilizations.FirstOrDefault(c => c.Id == _galaxy.PlayerCivilizationId);
        if (profile is null || civilization is null || string.IsNullOrWhiteSpace(name) || name.Length > 160) return false;
        civilization.Leadership.Assign(role.ToString(), new CivilizationCharacter(
            $"civ-{civilization.Id}:{role}:appointed:{profile.Id}", name.Trim(), profile.Id, profile.Portrait));
        return true;
    }

    public bool UiTestVoiceEvent(string eventKey)
    {
        if (!UiIsDeveloperMode || _voiceEvents is null || UiVoice is null) return false;
        var player = _galaxy.Civilizations.Single(c => c.Id == _galaxy.PlayerCivilizationId);
        var values = new Dictionary<string, string>
        {
            ["detail"] = "Orbital Industry", ["research_name"] = "Orbital Industry", ["project_name"] = "Orbital Shipyard",
            ["ship_name"] = "ISS Horizon", ["ship_class"] = "Pathfinder Scout", ["colony_name"] = "Mars",
            ["planet_name"] = "Mars", ["system_name"] = "Sol", ["fleet_name"] = "First Fleet",
            ["species_name"] = "Unknown civilization", ["civilization_name"] = player.Name,
            ["enemy_name"] = "Hostile fleet", ["resource_name"] = "Industry", ["amount"] = "20",
            ["message"] = "Your transmission has been received. We are ready to begin discussions.",
            ["destination_name"] = "Alpha Centauri",
            ["object_name"] = "Intergalactic Object 17",
        };
        // This explicitly labeled Developer sample uses a fixed fictional sender, not hidden
        // live civilization data, and cannot establish contact or change diplomacy.
        var alienSample = eventKey == "diplomacy.alien.transmission";
        return _voiceEvents.Emit(new GameplayVoiceEvent(eventKey, alienSample ? -100 : player.Id,
            "voice-lab:" + Time.GetTicksMsec(), values, (long)_clock.SimulationDays, "Developer sample")
        {
            SourceSpeciesId = alienSample ? "pelagic_high_pressure" : player.SpeciesId,
            Audience = alienSample ? VoiceAudience.DirectCommunication : VoiceAudience.OwnCivilization,
            RecipientCivilizationId = alienSample ? player.Id : null,
            Overrides = new VoiceEventOverrides { Category = "voice-lab-event:" + eventKey, CooldownSeconds = 0 },
        }, new VoiceRoutingContext(player.Id, VoiceFrequency.Frequent));
    }
}
