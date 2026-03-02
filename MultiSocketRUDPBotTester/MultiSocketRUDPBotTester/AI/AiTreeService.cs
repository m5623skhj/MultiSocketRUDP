using System.Text.Json;
using MultiSocketRUDPBotTester.Bot;
using Serilog;

namespace MultiSocketRUDPBotTester.AI
{
    public class AiTreeService
    {
        private static readonly string[] KnownNodeTypes =
        [
            "SendPacketNode", "DelayNode", "RandomDelayNode", "LogNode",
            "DisconnectNode", "ConditionalNode", "LoopNode", "RepeatTimerNode",
            "WaitForPacketNode", "SetVariableNode", "GetVariableNode",
            "CustomActionNode", "AssertNode", "RetryNode", "PacketParserNode"
        ];

        public AiTreeResponse Parse(string aiResponse)
        {
            try
            {
                var cleaned = StripMarkdownFence(aiResponse.Trim());

                var jsonStart = cleaned.IndexOf('{');
                var jsonEnd = cleaned.LastIndexOf('}');

                if (jsonStart < 0 || jsonEnd < 0 || jsonStart >= jsonEnd)
                {
                    return AiTreeResponse.Fail(
                        "Invalid response format",
                        $"Could not find valid JSON in AI response.\n\nResponse:\n{Truncate(aiResponse)}");
                }

                var jsonContent = cleaned[jsonStart..(jsonEnd + 1)];
                Log.Debug("Extracted JSON: {Json}", jsonContent);

                var doc = JsonDocument.Parse(jsonContent);
                var root = doc.RootElement;

                if (root.TryGetProperty("error", out var errorEl))
                    return AiTreeResponse.Fail(
                        errorEl.GetProperty("reason").GetString() ?? "Unknown error",
                        errorEl.GetProperty("details").GetString() ?? "");

                var description = root.TryGetProperty("description", out var desc)
                    ? desc.GetString() ?? "" : "";

                var treeElement = root.TryGetProperty("tree", out var tree) ? tree : root;

                return new AiTreeResponse
                {
                    IsError = false,
                    TreeJson = jsonContent,
                    Description = description,
                    RootNode = treeElement
                };
            }
            catch (JsonException ex)
            {
                return AiTreeResponse.Fail(
                    "JSON Parse Error",
                    $"Failed to parse AI response as JSON: {ex.Message}\n\nResponse:\n{Truncate(aiResponse)}");
            }
            catch (Exception ex)
            {
                return AiTreeResponse.Fail("Parse Error",
                    $"{ex.Message}\n\nResponse:\n{Truncate(aiResponse)}");
            }
        }

        public AiTreeValidationResult Validate(AiTreeResponse response)
        {
            var result = new AiTreeValidationResult();

            try
            {
                if (response.RootNode.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined)
                {
                    result.IsValid = false;
                    result.Errors.Add("Tree is empty or invalid");
                    return result;
                }

                ValidateNode(response.RootNode, result, "root");
            }
            catch (Exception ex)
            {
                result.IsValid = false;
                result.Errors.Add($"Validation exception: {ex.Message}");
            }

            return result;
        }

        private static void ValidateNode(JsonElement node, AiTreeValidationResult result, string path)
        {
            if (!node.TryGetProperty("type", out var typeProp))
            {
                result.IsValid = false;
                result.Errors.Add($"Node at '{path}' is missing 'type' property");
                return;
            }

            var nodeType = typeProp.GetString();
            if (string.IsNullOrEmpty(nodeType))
            {
                result.IsValid = false;
                result.Errors.Add($"Node at '{path}' has empty type");
                return;
            }

            if (!KnownNodeTypes.Contains(nodeType))
            {
                result.IsValid = false;
                result.Errors.Add($"Unknown node type at '{path}': {nodeType}");
            }

            foreach (var (key, childPath) in ChildProperties(path))
            {
                if (node.TryGetProperty(key, out var child))
                {
                    ValidateNode(child, result, childPath);
                }
            }
        }

        private static IEnumerable<(string key, string path)> ChildProperties(string basePath) =>
        [
            ("next",          $"{basePath}.next"),
            ("true_branch",   $"{basePath}.true_branch"),
            ("false_branch",  $"{basePath}.false_branch"),
            ("loop_body",     $"{basePath}.loop_body"),
            ("repeat_body",   $"{basePath}.repeat_body"),
            ("timeout_nodes", $"{basePath}.timeout_nodes")
        ];

        public string FormatJson(string json)
        {
            try
            {
                var doc = JsonDocument.Parse(json);
                return JsonSerializer.Serialize(doc, new JsonSerializerOptions { WriteIndented = true });
            }
            catch
            {
                return json;
            }
        }

        private static string StripMarkdownFence(string text)
        {
            if (text.StartsWith("```json"))
            {
                text = text[7..];
            }
            else if (text.StartsWith("```"))
            {
                text = text[3..];
            }

            if (text.EndsWith("```"))
            {
                text = text[..^3];
            }

            return text.Trim();
        }

        private static string Truncate(string s) =>
            s.Length > 500 ? s[..500] : s;
    }
}