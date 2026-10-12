using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace Fable2Launcher;

public static class LauncherConfigFile
{
    public static Dictionary<string, string> ReadLauncherValues(string directory)
    {
        var values = ReadValues(Path.Combine(directory, "fable_2.toml"));
        foreach ((string key, string value) in ReadValues(Path.Combine(directory, "launcher-settings.toml")))
            if (GraphicsSettings.ManagedKeys.Contains(key)) values[key] = value;
        return values;
    }

    private static readonly Regex ValuePattern = new(
        @"^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*(?:#.*)?$",
        RegexOptions.Compiled);

    private static readonly Regex AssignmentPattern = new(
        @"^(\s*)([A-Za-z0-9_]+)(\s*=\s*)(.*?)(\s+#.*)?$",
        RegexOptions.Compiled);

    public static Dictionary<string, string> ReadValues(string path)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (!File.Exists(path)) return values;

        foreach (string line in File.ReadLines(path))
        {
            // ReXGlue cvars are root keys. Do not mistake an unrelated table's
            // setting for a global option with the same name.
            if (line.TrimStart().StartsWith('[')) break;
            Match match = ValuePattern.Match(line);
            if (match.Success)
            {
                values[match.Groups[1].Value] = match.Groups[2].Value.Trim();
            }
        }
        return values;
    }

    public static void WriteValues(string path, IReadOnlyDictionary<string, string> settings,
        IReadOnlyList<string> keyOrder)
    {
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);

        string backupPath = path + ".launcher-backup";
        if (File.Exists(path) && !File.Exists(backupPath)) File.Copy(path, backupPath);

        List<string> lines = File.Exists(path)
            ? File.ReadAllLines(path).ToList()
            : new List<string> { "# Fable II Recomp Launcher configuration" };

        var updated = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int rootEnd = lines.FindIndex(line => line.TrimStart().StartsWith('['));
        if (rootEnd < 0) rootEnd = lines.Count;
        for (int i = 0; i < rootEnd; i++)
        {
            Match match = AssignmentPattern.Match(lines[i]);
            if (!match.Success) continue;

            string key = match.Groups[2].Value;
            if (!settings.TryGetValue(key, out string? value)) continue;

            // Retain indentation, spacing and any existing inline explanation.
            lines[i] = match.Groups[1].Value + key + match.Groups[3].Value + value +
                       match.Groups[5].Value;
            updated.Add(key);
        }

        var appended = new List<string>();
        if (rootEnd > 0 && !string.IsNullOrWhiteSpace(lines[rootEnd - 1])) appended.Add(string.Empty);
        foreach (string key in keyOrder)
        {
            if (!updated.Contains(key) && settings.TryGetValue(key, out string? value))
            {
                appended.Add($"{key} = {value}");
            }
        }
        if (rootEnd < lines.Count && appended.Count > 0) appended.Add(string.Empty);
        lines.InsertRange(rootEnd, appended);

        string temporaryPath = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(temporaryPath, string.Join(Environment.NewLine, lines) + Environment.NewLine,
                new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            File.Move(temporaryPath, path, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
        }
    }
}
