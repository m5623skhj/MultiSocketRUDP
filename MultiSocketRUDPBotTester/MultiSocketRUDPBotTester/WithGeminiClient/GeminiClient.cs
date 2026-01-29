using Google.GenAI;
using Google.GenAI.Types;
using Microsoft.Extensions.Configuration;

namespace MultiSocketRUDPBotTester.WithGeminiClient;

public static class GeminiExtensions
{
    public static string? GetText(this GenerateContentResponse response)
    {
        return response.Candidates?[0].Content?.Parts?[0].Text;
    }
}

public class GeminiClient
{
    private readonly Client client;
    private readonly string modelName;
    private readonly string systemPrompt;

    public GeminiClient(IConfiguration configuration)
    {
        var apiKey = configuration["GeminiSettings:ApiKey"]
                     ?? throw new ArgumentException("API Key is missing.");
        modelName = configuration["GeminiSettings:ModelName"] ?? "gemini-2.0-flash";
        systemPrompt = configuration["GeminiSettings:SystemPrompt"]
                    ?? throw new ArgumentException("SystemPrompt is missing.");

        client = new Client(apiKey: apiKey);
    }

    public async Task<string> AskAsync(string userMessage)
    {
        try
        {
            var response = await client.Models.GenerateContentAsync(
                modelName,
                [new Content { Role = "user", Parts = [new Part { Text = userMessage }] }],
                new GenerateContentConfig
                {
                    SystemInstruction = new Content { Parts = [new Part { Text = systemPrompt }] }
                }
            );

            return response.GetText() ?? "응답을 생성할 수 없습니다.";
        }
        catch (Exception ex)
        {
            return $"[에러 발생]: {ex.Message}";
        }
    }
}