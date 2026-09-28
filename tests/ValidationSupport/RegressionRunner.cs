using System;
using System.IO;
using System.Linq;
using System.Reflection;

namespace Game.Validation;

[AttributeUsage(AttributeTargets.Method)]
internal sealed class RegressionCheckAttribute : Attribute { }

/// <summary>Runs regression groups after Main starts, so a failed assertion is a terminal failure,
/// never an uncaught CLR module-initializer crash. No checks are omitted after a failure.</summary>
internal static class RegressionRunner
{
    public static int Count { get; private set; }
    public static int Run(Assembly assembly)
    {
        var failures = 0; Count = 0;
        foreach (var method in assembly.GetTypes().SelectMany(t => t.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic))
                     .Where(m => m.IsDefined(typeof(RegressionCheckAttribute))).OrderBy(m => m.MetadataToken))
        {
            Count++;
            try { method.Invoke(null, null); }
            catch (Exception error)
            {
                failures++;
                Report(method.DeclaringType!.Name + "." + method.Name,
                    error is TargetInvocationException { InnerException: { } inner } ? inner : error);
            }
        }
        return failures;
    }
    public static void Report(string name, Exception error) => Console.Error.WriteLine(
        $"FAIL: {name}\nWorking directory: {Directory.GetCurrentDirectory()}\nAssembly: {typeof(RegressionRunner).Assembly.Location}\n{error}");
}
