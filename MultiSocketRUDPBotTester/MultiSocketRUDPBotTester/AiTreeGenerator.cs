using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private Window? aiGeneratorWindow;

        private void ShowAiTreeGenerator()
        {
            if (aiGeneratorWindow?.IsVisible == true)
            {
                aiGeneratorWindow.Activate();
                return;
            }

            var window = new Window
            {
                Title = "AI Tree Generator",
                Width = 700,
                Height = 600,
                Owner = this,
                WindowStartupLocation = WindowStartupLocation.CenterOwner
            };

            var mainGrid = new Grid { Margin = new Thickness(10) };
            mainGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            mainGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            mainGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(200) });
            mainGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            var inputSection = new StackPanel();
            inputSection.Children.Add(new TextBlock
            {
                Text = "Describe the test scenario:",
                FontWeight = FontWeights.Bold,
                Margin = new Thickness(0, 0, 0, 5)
            });

            inputSection.Children.Add(new TextBlock
            {
                Text = "Example: \"If player has money, go to shop and buy item 1\"",
                FontSize = 11,
                Foreground = Brushes.Gray,
                Margin = new Thickness(0, 0, 0, 10)
            });

            var inputBox = new TextBox
            {
                Height = 100,
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Text = ""
            };
            inputSection.Children.Add(inputBox);

            Grid.SetRow(inputSection, 0);
            mainGrid.Children.Add(inputSection);

            var generateButton = new Button
            {
                Content = "Generate Tree",
                Height = 40,
                Margin = new Thickness(0, 10, 0, 10),
                Background = new SolidColorBrush(Color.FromRgb(33, 150, 243)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Bold,
                FontSize = 14
            };
            Grid.SetRow(generateButton, 1);
            mainGrid.Children.Add(generateButton);

            var outputSection = new StackPanel { Margin = new Thickness(0, 10, 0, 0) };
            outputSection.Children.Add(new TextBlock
            {
                Text = "AI Response:",
                FontWeight = FontWeights.Bold,
                Margin = new Thickness(0, 0, 0, 5)
            });

            var outputBox = new TextBox
            {
                Height = 180,
                IsReadOnly = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = new SolidColorBrush(Color.FromRgb(250, 250, 250))
            };
            outputSection.Children.Add(outputBox);

            Grid.SetRow(outputSection, 2);
            mainGrid.Children.Add(outputSection);

            var buttonPanel = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                HorizontalAlignment = HorizontalAlignment.Right,
                Margin = new Thickness(0, 10, 0, 0)
            };

            var applyButton = new Button
            {
                Content = "Apply to Canvas",
                Width = 120,
                Height = 35,
                Background = new SolidColorBrush(Color.FromRgb(76, 175, 80)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Bold,
                IsEnabled = false
            };

            var cancelButton = new Button
            {
                Content = "Close",
                Width = 80,
                Height = 35,
                Margin = new Thickness(10, 0, 0, 0)
            };
            cancelButton.Click += (_, _) => window.Close();

            buttonPanel.Children.Add(applyButton);
            buttonPanel.Children.Add(cancelButton);

            Grid.SetRow(buttonPanel, 3);
            mainGrid.Children.Add(buttonPanel);

            window.Content = mainGrid;

            AiTreeResponse? lastGeneratedTree = null;
            generateButton.Click += async (_, _) =>
            {
                var userInput = inputBox.Text.Trim();
                if (string.IsNullOrEmpty(userInput))
                {
                    MessageBox.Show("Please enter a test scenario description.", "Input Required",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                generateButton.IsEnabled = false;
                applyButton.IsEnabled = false;
                outputBox.Text = "Generating tree from AI...";

                try
                {
                    var prompt = $$"""

                                        User Request: "{{userInput}}"

                                        Task: Generate a behavior tree JSON based on the Node Specifications provided in your system instructions.
                                        Rules:
                                        1. Output ONLY valid JSON.
                                        2. No markdown tags (e.g., no ```json).
                                        3. Follow the schema: { "description": "string", "tree": { "type": "string", ... } }
                                        """;
                    var aiResponse = await geminiClient.AskAsync(prompt);
                    Log($"AI Response received: {aiResponse}");

                    var treeResponse = ParseAiResponse(aiResponse);
                    if (treeResponse.IsError)
                    {
                        outputBox.Text = $"Error: {treeResponse.ErrorReason}\n\n{treeResponse.ErrorDetails}";
                        Log($"AI Generation Error: {treeResponse.ErrorReason}");
                        lastGeneratedTree = null;
                    }
                    else
                    {
                        outputBox.Text = $"Description:\n{treeResponse.Description}\n\n";
                        outputBox.Text += $"Tree Structure:\n{FormatJsonForDisplay(treeResponse.TreeJson)}\n\n";

                        var validationResult = ValidateAiTree(treeResponse);
                        if (validationResult.IsValid)
                        {
                            lastGeneratedTree = treeResponse;
                            applyButton.IsEnabled = true;
                            outputBox.Text += "Tree validation passed. You can apply it to the canvas.";
                            Log($"AI tree generated successfully: {treeResponse.Description}");
                        }
                        else
                        {
                            outputBox.Text += $"Validation failed:\n{string.Join("\n", validationResult.Errors)}";
                            Log($"AI tree validation failed: {string.Join(", ", validationResult.Errors)}");
                            lastGeneratedTree = null;
                        }
                    }
                }
                catch (Exception ex)
                {
                    outputBox.Text = $"Error occurred: {ex.Message}\n\nStack trace:\n{ex.StackTrace}";
                    Log($"AI tree generation error: {ex.Message}");
                    lastGeneratedTree = null;
                }
                finally
                {
                    generateButton.IsEnabled = true;
                }
            };

            applyButton.Click += (_, _) =>
            {
                if (lastGeneratedTree == null)
                {
                    return;
                }

                try
                {
                    ClearAllNodesExceptRoot();
                    CreateNodesFromAiTree(lastGeneratedTree);

                    MessageBox.Show("Tree has been applied to the canvas!\n\nYou can now validate and build the graph.", "Success",
                        MessageBoxButton.OK, MessageBoxImage.Information);

                    window.Close();
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Failed to apply tree: {ex.Message}\n\nStack trace:\n{ex.StackTrace}", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    Log($"Failed to apply AI tree: {ex.Message}");
                }
            };

            aiGeneratorWindow = window;
            window.Closed += (_, _) => aiGeneratorWindow = null;
            window.Show();
        }

        private string FormatJsonForDisplay(string json)
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

        private AiTreeResponse ParseAiResponse(string aiResponse)
        {
            try
            {
                var cleanedResponse = aiResponse.Trim();
                if (cleanedResponse.StartsWith("```json"))
                {
                    cleanedResponse = cleanedResponse.Substring(7);
                }
                else if (cleanedResponse.StartsWith("```"))
                {
                    cleanedResponse = cleanedResponse.Substring(3);
                }

                if (cleanedResponse.EndsWith("```"))
                {
                    cleanedResponse = cleanedResponse.Substring(0, cleanedResponse.Length - 3);
                }

                cleanedResponse = cleanedResponse.Trim();

                var jsonStart = cleanedResponse.IndexOf('{');
                var jsonEnd = cleanedResponse.LastIndexOf('}');

                if (jsonStart < 0 || jsonEnd < 0 || jsonStart >= jsonEnd)
                {
                    return new AiTreeResponse
                    {
                        IsError = true,
                        ErrorReason = "Invalid response format",
                        ErrorDetails = $"Could not find valid JSON in AI response. Response: {aiResponse.Substring(0, Math.Min(500, aiResponse.Length))}"
                    };
                }

                var jsonContent = cleanedResponse.Substring(jsonStart, jsonEnd - jsonStart + 1);

                Log($"Extracted JSON: {jsonContent}");

                var doc = JsonDocument.Parse(jsonContent);
                var root = doc.RootElement;

                if (root.TryGetProperty("error", out var errorElement))
                {
                    return new AiTreeResponse
                    {
                        IsError = true,
                        ErrorReason = errorElement.GetProperty("reason").GetString() ?? "Unknown error",
                        ErrorDetails = errorElement.GetProperty("details").GetString() ?? ""
                    };
                }

                var description = "";
                if (root.TryGetProperty("description", out var descProp))
                {
                    description = descProp.GetString() ?? "";
                }

                var treeElement = root.TryGetProperty("tree", out var treeProp) ? treeProp : root;
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
                return new AiTreeResponse
                {
                    IsError = true,
                    ErrorReason = "JSON Parse Error",
                    ErrorDetails = $"Failed to parse AI response as JSON: {ex.Message}\n\nResponse: {aiResponse.Substring(0, Math.Min(500, aiResponse.Length))}"
                };
            }
            catch (Exception ex)
            {
                return new AiTreeResponse
                {
                    IsError = true,
                    ErrorReason = "Parse Error",
                    ErrorDetails = $"{ex.Message}\n\nResponse: {aiResponse[..Math.Min(500, aiResponse.Length)]}"
                };
            }
        }

        private AiTreeValidationResult ValidateAiTree(AiTreeResponse treeResponse)
        {
            var result = new AiTreeValidationResult { IsValid = true };

            try
            {
                if (treeResponse.RootNode.ValueKind is JsonValueKind.Null or JsonValueKind.Undefined)
                {
                    result.IsValid = false;
                    result.Errors.Add("Tree is empty or invalid");
                    return result;
                }

                ValidateNodeRecursive(treeResponse.RootNode, result, "root");
            }
            catch (Exception ex)
            {
                result.IsValid = false;
                result.Errors.Add($"Validation exception: {ex.Message}");
            }

            return result;
        }

        private void ValidateNodeRecursive(JsonElement node, AiTreeValidationResult result, string path)
        {
            if (!node.TryGetProperty("type", out var typeProperty))
            {
                result.IsValid = false;
                result.Errors.Add($"Node at '{path}' is missing 'type' property");
                return;
            }

            var nodeType = typeProperty.GetString();

            if (string.IsNullOrEmpty(nodeType))
            {
                result.IsValid = false;
                result.Errors.Add($"Node at '{path}' has empty type");
                return;
            }

            var knownTypes = new[]
            {
                "SendPacketNode", "DelayNode", "RandomDelayNode", "LogNode",
                "DisconnectNode", "ConditionalNode", "LoopNode", "RepeatTimerNode",
                "WaitForPacketNode", "SetVariableNode", "GetVariableNode",
                "CustomActionNode", "AssertNode", "RetryNode", "PacketParserNode"
            };

            if (!knownTypes.Contains(nodeType))
            {
                result.IsValid = false;
                result.Errors.Add($"Unknown node type at '{path}': {nodeType}");
            }

            if (node.TryGetProperty("next", out var nextNode))
            {
                ValidateNodeRecursive(nextNode, result, $"{path}.next");
            }

            if (node.TryGetProperty("true_branch", out var trueNode))
            {
                ValidateNodeRecursive(trueNode, result, $"{path}.true_branch");
            }

            if (node.TryGetProperty("false_branch", out var falseNode))
            {
                ValidateNodeRecursive(falseNode, result, $"{path}.false_branch");
            }

            if (node.TryGetProperty("loop_body", out var loopBody))
            {
                ValidateNodeRecursive(loopBody, result, $"{path}.loop_body");
            }

            if (node.TryGetProperty("repeat_body", out var repeatBody))
            {
                ValidateNodeRecursive(repeatBody, result, $"{path}.repeat_body");
            }

            if (node.TryGetProperty("timeout_nodes", out var timeoutNodes))
            {
                ValidateNodeRecursive(timeoutNodes, result, $"{path}.timeout_nodes");
            }
        }

        private void CreateNodesFromAiTree(AiTreeResponse treeResponse)
        {
            var rootVisual = allNodes.FirstOrDefault(n => n.IsRoot);
            if (rootVisual == null)
            {
                throw new InvalidOperationException("Root node not found");
            }

            var startX = Canvas.GetLeft(rootVisual.Border) + 200;
            var startY = Canvas.GetTop(rootVisual.Border);

            var createdNodes = new Dictionary<string, NodeVisual>();

            var firstNode = CreateNodeVisualFromJson(treeResponse.RootNode, null, startX, startY, createdNodes, "root");

            if (firstNode != null)
            {
                rootVisual.Next = firstNode;
                rootVisual.NextPortType = "default";
            }

            RedrawConnections();
            Log($"AI tree applied to canvas with {createdNodes.Count} nodes");
        }

        private NodeVisual? CreateNodeVisualFromJson(
            JsonElement jsonNode,
            NodeVisual? parentNode,
            double x,
            double y,
            Dictionary<string, NodeVisual> createdNodes,
            string nodePath)
        {
            if (!jsonNode.TryGetProperty("type", out var typeProperty))
            {
                Log($"Warning: Node at '{nodePath}' has no type property");
                return null;
            }

            var nodeTypeName = typeProperty.GetString();
            if (string.IsNullOrEmpty(nodeTypeName))
            {
                Log($"Warning: Node at '{nodePath}' has empty type");
                return null;
            }

            var nodeType = Type.GetType($"MultiSocketRUDPBotTester.Bot.{nodeTypeName}");

            if (nodeType == null)
            {
                Log($"Warning: Could not find type {nodeTypeName} at '{nodePath}'");
                return null;
            }

            var category = NodeCategory.Action;
            if (nodeTypeName.Contains("Condition"))
            {
                category = NodeCategory.Condition;
            }
            else if (nodeTypeName.Contains("Loop") || nodeTypeName.Contains("Repeat") ||
                     nodeTypeName == "WaitForPacketNode" || nodeTypeName == "RetryNode" ||
                     nodeTypeName == "AssertNode")
            {
                category = NodeCategory.Loop;
            }

            var border = CreateNodeVisual(nodeTypeName, GetNodeColor(category));
            var visual = new NodeVisual
            {
                Border = border,
                Category = category,
                InputPort = CreateInputPort(),
                NodeType = nodeType,
                Configuration = new NodeConfiguration()
            };

            if (category == NodeCategory.Action)
            {
                visual.OutputPort = CreateOutputPort("default");
            }
            else
            {
                visual.OutputPortTrue = CreateOutputPort(category == NodeCategory.Condition ? "true" : "continue");
                visual.OutputPortFalse = CreateOutputPort(category == NodeCategory.Condition ? "false" : "exit");
            }

            ConfigureNodeFromJson(visual, jsonNode);
            Canvas.SetLeft(border, x);
            Canvas.SetTop(border, y);
            AddNodeToCanvas(visual);

            createdNodes[nodePath] = visual;
            var childX = x + 200;
            var baseChildY = y;

            if (jsonNode.TryGetProperty("next", out var nextNode))
            {
                var nextVisual = CreateNodeVisualFromJson(nextNode, visual, childX, baseChildY, createdNodes, $"{nodePath}.next");
                if (nextVisual != null && visual.Category == NodeCategory.Action)
                {
                    visual.Next = nextVisual;
                    visual.NextPortType = "default";
                }
            }

            if (jsonNode.TryGetProperty("true_branch", out var trueNode))
            {
                var trueVisual = CreateNodeVisualFromJson(trueNode, null, childX, baseChildY, createdNodes, $"{nodePath}.true_branch");
                if (trueVisual != null)
                {
                    visual.TrueChild = trueVisual;
                    visual.TruePortType = "true";
                }
                baseChildY += 150;
            }

            if (jsonNode.TryGetProperty("false_branch", out var falseNode))
            {
                var falseVisual = CreateNodeVisualFromJson(falseNode, null, childX, baseChildY, createdNodes, $"{nodePath}.false_branch");
                if (falseVisual != null)
                {
                    visual.FalseChild = falseVisual;
                    visual.FalsePortType = "false";
                }
                baseChildY += 150;
            }

            if (jsonNode.TryGetProperty("loop_body", out var loopBody))
            {
                var loopVisual = CreateNodeVisualFromJson(loopBody, null, childX, baseChildY, createdNodes, $"{nodePath}.loop_body");
                if (loopVisual != null)
                {
                    visual.TrueChild = loopVisual;
                    visual.TruePortType = "continue";
                }
                baseChildY += 150;
            }

            if (jsonNode.TryGetProperty("repeat_body", out var repeatBody))
            {
                var repeatVisual = CreateNodeVisualFromJson(repeatBody, null, childX, baseChildY, createdNodes, $"{nodePath}.repeat_body");
                if (repeatVisual != null)
                {
                    visual.TrueChild = repeatVisual;
                    visual.TruePortType = "continue";
                }
            }

            if (jsonNode.TryGetProperty("timeout_nodes", out var timeoutNodes))
            {
                var timeoutVisual = CreateNodeVisualFromJson(timeoutNodes, null, childX, baseChildY, createdNodes, $"{nodePath}.timeout_nodes");
                if (timeoutVisual != null)
                {
                    visual.FalseChild = timeoutVisual;
                    visual.FalsePortType = "exit";
                }
            }

            return visual;
        }

        private void ConfigureNodeFromJson(NodeVisual visual, JsonElement jsonNode)
        {
            if (visual.Configuration == null)
            {
                return;
            }

            try
            {
                if (visual.NodeType == typeof(SendPacketNode))
                {
                    if (!jsonNode.TryGetProperty("packet_id", out var packetIdProp))
                    {
                        return;
                    }

                    var packetIdStr = packetIdProp.GetString();
                    if (!string.IsNullOrEmpty(packetIdStr) && Enum.TryParse<PacketId>(packetIdStr, true, out var packetId))
                    {
                        visual.Configuration.PacketId = packetId;
                    }
                }
                else if (visual.NodeType == typeof(DelayNode))
                {
                    if (jsonNode.TryGetProperty("delay_ms", out var delayProp))
                    {
                        visual.Configuration.IntValue = delayProp.GetInt32();
                    }
                }
                else if (visual.NodeType == typeof(RandomDelayNode))
                {
                    if (jsonNode.TryGetProperty("min_delay_ms", out var minProp))
                    {
                        visual.Configuration.Properties["MinDelay"] = minProp.GetInt32();
                    }
                    if (jsonNode.TryGetProperty("max_delay_ms", out var maxProp))
                    {
                        visual.Configuration.Properties["MaxDelay"] = maxProp.GetInt32();
                    }
                }
                else if (visual.NodeType == typeof(LogNode))
                {
                    if (jsonNode.TryGetProperty("message", out var messageProp))
                    {
                        visual.Configuration.StringValue = messageProp.GetString() ?? "";
                    }
                }
                else if (visual.NodeType == typeof(DisconnectNode))
                {
                    if (jsonNode.TryGetProperty("reason", out var reasonProp))
                    {
                        visual.Configuration.StringValue = reasonProp.GetString() ?? "User requested disconnect";
                    }
                }
                else if (visual.NodeType == typeof(ConditionalNode))
                {
                    if (!jsonNode.TryGetProperty("condition", out var conditionProp))
                    {
                        return;
                    }

                    var condition = conditionProp.GetString() ?? "";
                    visual.Configuration.Properties["Left"] = condition;
                    visual.Configuration.Properties["Op"] = "==";
                    visual.Configuration.Properties["Right"] = "true";
                    visual.Configuration.Properties["LeftType"] = "Constant";
                    visual.Configuration.Properties["RightType"] = "Constant";
                }
                else if (visual.NodeType == typeof(LoopNode))
                {
                    if (jsonNode.TryGetProperty("max_iterations", out var maxItorProp))
                    {
                        visual.Configuration.Properties["LoopCount"] = maxItorProp.GetInt32();
                    }
                }
                else if (visual.NodeType == typeof(RepeatTimerNode))
                {
                    if (jsonNode.TryGetProperty("repeat_count", out var countProp))
                    {
                        visual.Configuration.Properties["RepeatCount"] = countProp.GetInt32();
                    }
                    if (jsonNode.TryGetProperty("interval_ms", out var intervalProp))
                    {
                        visual.Configuration.IntValue = intervalProp.GetInt32();
                    }
                }
                else if (visual.NodeType == typeof(WaitForPacketNode))
                {
                    if (jsonNode.TryGetProperty("expected_packet_id", out var expectedProp))
                    {
                        var packetIdStr = expectedProp.GetString();
                        if (!string.IsNullOrEmpty(packetIdStr) && Enum.TryParse<PacketId>(packetIdStr, true, out var packetId))
                        {
                            visual.Configuration.PacketId = packetId;
                        }
                    }
                    if (jsonNode.TryGetProperty("timeout_ms", out var timeoutProp))
                    {
                        visual.Configuration.IntValue = timeoutProp.GetInt32();
                    }
                }
                else if (visual.NodeType == typeof(SetVariableNode))
                {
                    if (jsonNode.TryGetProperty("variable_name", out var varNameProp))
                    {
                        visual.Configuration.Properties["VariableName"] = varNameProp.GetString() ?? "value";
                    }
                    if (jsonNode.TryGetProperty("value_type", out var valueTypeProp))
                    {
                        visual.Configuration.Properties["ValueType"] = valueTypeProp.GetString() ?? "int";
                    }
                    if (jsonNode.TryGetProperty("value", out var valueProp))
                    {
                        visual.Configuration.Properties["Value"] = valueProp.GetString() ?? "0";
                    }
                }
                else if (visual.NodeType == typeof(GetVariableNode))
                {
                    if (jsonNode.TryGetProperty("variable_name", out var varNameProp))
                    {
                        visual.Configuration.StringValue = varNameProp.GetString() ?? "value";
                    }
                }
            }
            catch (Exception ex)
            {
                Log($"Warning: Failed to configure node {visual.NodeType?.Name}: {ex.Message}");
            }
        }

        private void ClearAllNodesExceptRoot()
        {
            var nodesToRemove = allNodes.Where(n => !n.IsRoot).ToList();

            foreach (var node in nodesToRemove)
            {
                GraphCanvas.Children.Remove(node.Border);
                GraphCanvas.Children.Remove(node.InputPort);
                if (node.OutputPort != null) GraphCanvas.Children.Remove(node.OutputPort);
                if (node.OutputPortTrue != null) GraphCanvas.Children.Remove(node.OutputPortTrue);
                if (node.OutputPortFalse != null) GraphCanvas.Children.Remove(node.OutputPortFalse);

                foreach (var dynamicPort in node.DynamicOutputPorts)
                {
                    GraphCanvas.Children.Remove(dynamicPort);
                }
            }

            allNodes.RemoveAll(n => !n.IsRoot);

            var rootNode = allNodes.FirstOrDefault(n => n.IsRoot);
            if (rootNode != null)
            {
                rootNode.Next = null;
                rootNode.TrueChild = null;
                rootNode.FalseChild = null;
                rootNode.NextPortType = null;
                rootNode.TruePortType = null;
                rootNode.FalsePortType = null;
            }

            RedrawConnections();
            Log("Cleared all nodes except root");
        }
    }

    public class AiTreeResponse
    {
        public bool IsError { get; set; }
        public string ErrorReason { get; set; } = "";
        public string ErrorDetails { get; set; } = "";
        public string TreeJson { get; set; } = "";
        public string Description { get; set; } = "";
        public JsonElement RootNode { get; set; }
    }

    public class AiTreeValidationResult
    {
        public bool IsValid { get; set; }
        public List<string> Errors { get; } = [];
    }
}