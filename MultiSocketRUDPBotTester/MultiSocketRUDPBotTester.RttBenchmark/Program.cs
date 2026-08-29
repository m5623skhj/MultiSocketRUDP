using System.Text.Json;
using MultiSocketRUDPBotTester.RttBenchmark;

try
{
    if (args.Length > 0 && string.Equals(args[0], "stress", StringComparison.OrdinalIgnoreCase))
    {
        var stressOptions = RttStressBenchmarkOptions.Parse(args);
        var stressResult = await RttStressBenchmarkExecutor.RunAsync(stressOptions);
        await WriteResultAsync(stressOptions.OutputPath, stressResult);
        var summary = stressResult.Summary;
        return summary.ConnectionFailures == 0 &&
            summary.RuntimeDisconnects == 0 &&
            summary.SendFailures == 0
            ? 0
            : 2;
    }

    var options = RttBenchmarkOptions.Parse(args);
    var result = await RttBenchmarkExecutor.RunAsync(options);
    await WriteResultAsync(options.OutputPath, result);

    Console.WriteLine(
        $"{result.ScenarioName}: P95={result.Aggregate.MedianP95RttMs:F6} ms " +
        $"P99={result.Aggregate.MedianP99RttMs:F6} ms " +
        $"Avg={result.Aggregate.MedianAverageRttMs:F6} ms");
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    Console.Error.WriteLine(
        args.Length > 0 && string.Equals(args[0], "stress", StringComparison.OrdinalIgnoreCase)
            ? RttStressBenchmarkOptions.Usage
            : RttBenchmarkOptions.Usage);
    return 1;
}

static async Task WriteResultAsync<T>(string inOutputPath, T inResult)
{
    var outputDirectory = Path.GetDirectoryName(Path.GetFullPath(inOutputPath));
    if (!string.IsNullOrEmpty(outputDirectory))
    {
        Directory.CreateDirectory(outputDirectory);
    }

    var jsonOptions = new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    await File.WriteAllTextAsync(
        inOutputPath,
        JsonSerializer.Serialize(inResult, jsonOptions));
}
