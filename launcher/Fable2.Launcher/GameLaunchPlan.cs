using System.IO;
using System.Text.Json;
using Fable2Launcher.Constants;

namespace Fable2Launcher;

public sealed record GameLaunchPlan(string Executable, string WorkingDirectory, string GameRoot);

public static class GameLaunchPlanner
{
    public static GameLaunchPlan Create(string executableDirectory, string gameRoot, GameCompatibility compatibility)
    {
        if (!compatibility.Supported || compatibility.Profile is null)
            throw new InvalidOperationException(compatibility.Message);
        string executable = Path.Combine(executableDirectory, "fable_2.exe");
        if (!File.Exists(executable))
            throw new InvalidOperationException("fable_2.exe was not found next to the launcher or in the selected folder.");

        // Legacy executables only support USA/EU. A generated descriptor permits
        // the German or dual-GOTY executable; never silently try an old US build.
        string descriptor = Path.Combine(executableDirectory, "fable2_build.json");
        string[] profiles = [GameVersions.DefaultProfile];
        if (File.Exists(descriptor))
        {
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(descriptor));
            profiles = document.RootElement.GetProperty("profiles").EnumerateArray()
                .Select(value => value.GetString() ?? "").ToArray();
        }
        if (!profiles.Contains(compatibility.Profile, StringComparer.Ordinal))
            throw new InvalidOperationException($"This fable_2.exe does not support {compatibility.Profile}. Install the dual-GOTY Recomp build.");
        return new(Path.GetFullPath(executable), Path.GetFullPath(executableDirectory), Path.GetFullPath(gameRoot));
    }
}
