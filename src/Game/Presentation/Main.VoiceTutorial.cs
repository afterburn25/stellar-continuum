using System;
using Game.Presentation.Audio.Voice;

namespace Game.Presentation;

public partial class Main
{
    public VoiceTutorialLayer? UiTutorial { get; private set; }
    protected void InitializeVoiceTutorial()
    {
        UiTutorial = new VoiceTutorialLayer { Name = "VoiceTutorialLayer" };
        AddChild(UiTutorial);
    }
    public void UiStartVoiceTutorial(bool restart = false) => UiTutorial?.Start(restart);
    public void UiSpeakTutorialLesson(VoiceTutorialLesson lesson, long revision)
    {
        if (_galaxy is null || UiVoice is null || !Enum.TryParse<VoiceSpeakerRole>(lesson.Role, out var role)) return;
        var profiles = new VoiceProfileRegistry(UiVoice.Profiles);
        var resolver = CharacterVoiceResolver.FromJson(profiles,
            Godot.FileAccess.GetFileAsString("res://data/voice_profiles/roles.json"), ResolveCurrentVoiceCharacter);
        UiVoice.Speak(new SpeechRequest(lesson.Profile, lesson.Text)
        {
            Category = "tutorial", Priority = (int)VoicePriority.Urgent,
            DedupeKey = $"tutorial:{UiCampaignApplicationRevision}:{lesson.Id}:{revision}",
            EventId = "tutorial." + lesson.Id, LocalizationKey = "tutorial." + lesson.Id,
            QueueBehavior = SpeechQueueBehavior.ReplaceCategory, Interruptible = true,
            CommunicationsFilterOverride = false,
            SpeakerContext = new VoiceSpeakerContext(role, PlayerCivilization.Id, PlayerCivilization.SpeciesId),
            SpeakerResolver = resolver.Resolve,
        });
    }
}
