using System;
using System.Collections.Generic;

namespace Game.Presentation;

public sealed record UiPlayerNotification(long Sequence, string Category, string Date, string Message, int? DiplomaticContactId = null);

/// <summary>Bounded player-visible session history. Callers must filter observer-sensitive
/// simulation events before publishing them into this feed.</summary>
public sealed class PlayerNotificationFeed
{
    public const int MaxItems = 32;
    private readonly Queue<UiPlayerNotification> _items = new();
    private long _nextSequence = 1;

    public IReadOnlyList<UiPlayerNotification> Items => _items.ToArray();

    public void Publish(string category, string date, string message, int? diplomaticContactId = null)
    {
        if (string.IsNullOrWhiteSpace(category)) throw new ArgumentException("Notification category is required.", nameof(category));
        if (string.IsNullOrWhiteSpace(date)) throw new ArgumentException("Notification date is required.", nameof(date));
        if (string.IsNullOrWhiteSpace(message)) throw new ArgumentException("Notification message is required.", nameof(message));
        _items.Enqueue(new UiPlayerNotification(_nextSequence++, category.Trim(), date.Trim(), message.Trim(), diplomaticContactId));
        while (_items.Count > MaxItems) _items.Dequeue();
    }

    public void Clear()
    {
        _items.Clear();
    }
}
