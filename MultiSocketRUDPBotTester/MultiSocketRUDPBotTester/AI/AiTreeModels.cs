using System.Text.Json;

namespace MultiSocketRUDPBotTester.AI
{
    public class AiTreeResponse
    {
        public bool IsError { get; set; }
        public string ErrorReason { get; set; } = "";
        public string ErrorDetails { get; set; } = "";
        public string TreeJson { get; set; } = "";
        public string Description { get; set; } = "";
        public JsonElement RootNode { get; set; }

        public static AiTreeResponse Fail(string reason, string details) => new()
        {
            IsError = true,
            ErrorReason = reason,
            ErrorDetails = details
        };
    }

    public class AiTreeValidationResult
    {
        public bool IsValid { get; set; } = true;
        public List<string> Errors { get; } = [];
    }
}