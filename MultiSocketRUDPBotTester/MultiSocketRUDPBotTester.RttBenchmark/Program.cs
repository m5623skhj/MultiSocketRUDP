using System.Text.Json;
using MultiSocketRUDPBotTester.RttBenchmark;

try
{
    var options = RttBenchmarkOptions.Parse(args);
    var result = await RttBenchmarkExecutor.RunAsync(options);
    var outputDirectory = Path.GetDirectoryName(Path.GetFullPath(options.OutputPath));
    if (!string.IsNullOrEmpty(outputDirectory))
    {
        Directory.CreateDirectory(outputDirectory);
    }

    var jsonOptions = new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    await File.WriteAllTextAsync(options.OutputPath, JsonSerializer.Serialize(result, jsonOptions));

    Console.WriteLine(
        $"{result.ScenarioName}: P95={result.Aggregate.MedianP95RttMs:F6} ms " +
        $"P99={result.Aggregate.MedianP99RttMs:F6} ms " +
        $"Avg={result.Aggregate.MedianAverageRttMs:F6} ms");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception.Message);
    Console.Error.WriteLine(RttBenchmarkOptions.Usage);
    return 1;
}
