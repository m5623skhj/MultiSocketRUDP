using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MultiSocketRUDPBotTester.AI;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private readonly AiTreeService aiTreeService = new();
        private Window? aiGeneratorWindow;

        private void ShowAiTreeGenerator()
        {
            if (aiGeneratorWindow?.IsVisible == true)
            {
                aiGeneratorWindow.Activate();
                return;
            }

            var window = BuildAiGeneratorWindow();
            aiGeneratorWindow = window;
            window.Closed += (_, _) => aiGeneratorWindow = null;
            window.Show();
        }

        private Window BuildAiGeneratorWindow()
        {
            var window = new Window
            {
                Title = "AI Tree Generator",
                Width = 700,
                Height = 600,
                Owner = this,
                WindowStartupLocation = WindowStartupLocation.CenterOwner
            };

            var grid = new Grid { Margin = new Thickness(10) };
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(200) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

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
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto
            };
            inputSection.Children.Add(inputBox);
            Grid.SetRow(inputSection, 0);
            grid.Children.Add(inputSection);

            var generateBtn = new Button
            {
                Content = "Generate Tree",
                Height = 40,
                Margin = new Thickness(0, 10, 0, 10),
                Background = new SolidColorBrush(Color.FromRgb(33, 150, 243)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Bold,
                FontSize = 14
            };
            Grid.SetRow(generateBtn, 1);
            grid.Children.Add(generateBtn);

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
            grid.Children.Add(outputSection);

            var applyBtn = new Button
            {
                Content = "Apply to Canvas",
                Width = 120,
                Height = 35,
                Background = new SolidColorBrush(Color.FromRgb(76, 175, 80)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Bold,
                IsEnabled = false
            };
            var closeBtn = new Button
            {
                Content = "Close",
                Width = 80,
                Height = 35,
                Margin = new Thickness(10, 0, 0, 0)
            };
            closeBtn.Click += (_, _) => window.Close();

            var btnPanel = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                HorizontalAlignment = HorizontalAlignment.Right,
                Margin = new Thickness(0, 10, 0, 0)
            };
            btnPanel.Children.Add(applyBtn);
            btnPanel.Children.Add(closeBtn);
            Grid.SetRow(btnPanel, 3);
            grid.Children.Add(btnPanel);

            window.Content = grid;

            AiTreeResponse? lastTree = null;

            generateBtn.Click += async (_, _) =>
                await HandleGenerate(inputBox, outputBox, generateBtn, applyBtn,
                    result => lastTree = result);

            applyBtn.Click += (_, _) =>
                HandleApply(lastTree, window);

            return window;
        }

        private async Task HandleGenerate(
            TextBox inputBox,
            TextBox outputBox,
            Button generateBtn,
            Button applyBtn,
            Action<AiTreeResponse?> setLastTree)
        {
            var userInput = inputBox.Text.Trim();
            if (string.IsNullOrEmpty(userInput))
            {
                MessageBox.Show("Please enter a test scenario description.", "Input Required",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            generateBtn.IsEnabled = false;
            applyBtn.IsEnabled = false;
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

                var treeResponse = aiTreeService.Parse(aiResponse);

                if (treeResponse.IsError)
                {
                    outputBox.Text = $"Error: {treeResponse.ErrorReason}\n\n{treeResponse.ErrorDetails}";
                    Log($"AI Generation Error: {treeResponse.ErrorReason}");
                    setLastTree(null);
                    return;
                }

                outputBox.Text = $"Description:\n{treeResponse.Description}\n\n";
                outputBox.Text += $"Tree Structure:\n{aiTreeService.FormatJson(treeResponse.TreeJson)}\n\n";

                var validation = aiTreeService.Validate(treeResponse);
                if (validation.IsValid)
                {
                    outputBox.Text += "✓ Tree validation passed. You can apply it to the canvas.";
                    Log($"AI tree generated successfully: {treeResponse.Description}");
                    setLastTree(treeResponse);
                    applyBtn.IsEnabled = true;
                }
                else
                {
                    outputBox.Text += $"✗ Validation failed:\n{string.Join("\n", validation.Errors)}";
                    Log($"AI tree validation failed: {string.Join(", ", validation.Errors)}");
                    setLastTree(null);
                }
            }
            catch (Exception ex)
            {
                outputBox.Text = $"Error occurred: {ex.Message}\n\nStack trace:\n{ex.StackTrace}";
                Log($"AI tree generation error: {ex.Message}");
                setLastTree(null);
            }
            finally
            {
                generateBtn.IsEnabled = true;
            }
        }

        private void HandleApply(AiTreeResponse? lastTree, Window window)
        {
            if (lastTree == null)
            {
                return;
            }

            try
            {
                ClearAllNodesExceptRoot();
                CreateNodesFromAiTree(lastTree);

                MessageBox.Show(
                    "Tree has been applied to the canvas!\n\nYou can now validate and build the graph.",
                    "Success", MessageBoxButton.OK, MessageBoxImage.Information);

                window.Close();
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    $"Failed to apply tree: {ex.Message}\n\nStack trace:\n{ex.StackTrace}",
                    "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                Log($"Failed to apply AI tree: {ex.Message}");
            }
        }

        private void ClearAllNodesExceptRoot()
        {
            var nodesToRemove = allNodes.Where(n => !n.IsRoot).ToList();

            foreach (var node in nodesToRemove)
            {
                GraphCanvas.Children.Remove(node.Border);
                GraphCanvas.Children.Remove(node.InputPort);
                if (node.OutputPort != null)
                {
                    GraphCanvas.Children.Remove(node.OutputPort);
                }

                if (node.OutputPortTrue != null)
                {
                    GraphCanvas.Children.Remove(node.OutputPortTrue);
                }

                if (node.OutputPortFalse != null)
                {
                    GraphCanvas.Children.Remove(node.OutputPortFalse);
                }

                foreach (var port in node.DynamicOutputPorts)
                {
                    GraphCanvas.Children.Remove(port);
                }
            }

            allNodes.RemoveAll(n => !n.IsRoot);

            var root = allNodes.FirstOrDefault(n => n.IsRoot);
            if (root != null)
            {
                root.Next = null;
                root.TrueChild = null;
                root.FalseChild = null;
                root.NextPortType = null;
                root.TruePortType = null;
                root.FalsePortType = null;
            }

            RedrawConnections();
            Log("Cleared all nodes except root");
        }

        private void CreateNodesFromAiTree(AiTreeResponse treeResponse)
        {
            var rootVisual = allNodes.FirstOrDefault(n => n.IsRoot)
                ?? throw new InvalidOperationException("Root node not found");

            var startX = Canvas.GetLeft(rootVisual.Border) + 200;
            var startY = Canvas.GetTop(rootVisual.Border);
            var createdNodes = new Dictionary<string, NodeVisual>();

            var firstNode = CreateNodeVisualFromJson(
                treeResponse.RootNode, startX, startY, createdNodes, "root");

            if (firstNode != null)
            {
                rootVisual.Next = firstNode;
                rootVisual.NextPortType = "default";
            }

            Dispatcher.InvokeAsync(() =>
            {
                RedrawConnections();
                Log($"AI tree applied to canvas with {createdNodes.Count} nodes");
            }, System.Windows.Threading.DispatcherPriority.Loaded);
        }

        private NodeVisual? CreateNodeVisualFromJson(
            JsonElement jsonNode,
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

            var category = nodeTypeName switch
            {
                _ when nodeTypeName.Contains("Condition") => NodeCategory.Condition,
                _ when nodeTypeName.Contains("Loop")
                       || nodeTypeName.Contains("Repeat")
                       || nodeTypeName == "WaitForPacketNode"
                       || nodeTypeName == "RetryNode"
                       || nodeTypeName == "AssertNode" => NodeCategory.Loop,
                _ => NodeCategory.Action
            };

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
                var nextVisual = CreateNodeVisualFromJson(
                    nextNode, childX, baseChildY, createdNodes, $"{nodePath}.next");
                if (nextVisual != null && visual.Category == NodeCategory.Action)
                {
                    visual.Next = nextVisual;
                    visual.NextPortType = "default";
                }
            }

            if (jsonNode.TryGetProperty("true_branch", out var trueNode))
            {
                var trueVisual = CreateNodeVisualFromJson(
                    trueNode, childX, baseChildY, createdNodes, $"{nodePath}.true_branch");
                if (trueVisual != null)
                {
                    visual.TrueChild = trueVisual;
                    visual.TruePortType = "true";
                }
                baseChildY += 150;
            }

            if (jsonNode.TryGetProperty("false_branch", out var falseNode))
            {
                var falseVisual = CreateNodeVisualFromJson(
                    falseNode, childX, baseChildY, createdNodes, $"{nodePath}.false_branch");
                if (falseVisual != null)
                {
                    visual.FalseChild = falseVisual;
                    visual.FalsePortType = "false";
                }
                baseChildY += 150;
            }

            if (jsonNode.TryGetProperty("loop_body", out var loopBody))
            {
                var loopVisual = CreateNodeVisualFromJson(
                    loopBody, childX, baseChildY, createdNodes, $"{nodePath}.loop_body");
                if (loopVisual != null)
                {
                    visual.TrueChild = loopVisual;
                    visual.TruePortType = "continue";
                }
                baseChildY += 150;
            }

            if (jsonNode.TryGetProperty("repeat_body", out var repeatBody))
            {
                var repeatVisual = CreateNodeVisualFromJson(
                    repeatBody, childX, baseChildY, createdNodes, $"{nodePath}.repeat_body");
                if (repeatVisual != null)
                {
                    visual.TrueChild = repeatVisual;
                    visual.TruePortType = "continue";
                }
            }

            if (!jsonNode.TryGetProperty("timeout_nodes", out var timeoutNodes))
            {
                return visual;
            }

            var timeoutVisual = CreateNodeVisualFromJson(
                timeoutNodes, childX, baseChildY, createdNodes, $"{nodePath}.timeout_nodes");
            if (timeoutVisual == null)
            {
                return visual;
            }

            visual.FalseChild = timeoutVisual;
            visual.FalsePortType = "exit";

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
                    if (!string.IsNullOrEmpty(packetIdStr) &&
                        Enum.TryParse<PacketId>(packetIdStr, true, out var packetId))
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
                    if (jsonNode.TryGetProperty("message", out var msgProp))
                    {
                        visual.Configuration.StringValue = msgProp.GetString() ?? "";
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
                    if (!jsonNode.TryGetProperty("condition", out var condProp))
                    {
                        return;
                    }

                    var condition = condProp.GetString() ?? "";
                    visual.Configuration.Properties["Left"] = condition;
                    visual.Configuration.Properties["Op"] = "==";
                    visual.Configuration.Properties["Right"] = "true";
                    visual.Configuration.Properties["LeftType"] = "Constant";
                    visual.Configuration.Properties["RightType"] = "Constant";
                }
                else if (visual.NodeType == typeof(LoopNode))
                {
                    if (jsonNode.TryGetProperty("max_iterations", out var maxItProp))
                    {
                        visual.Configuration.Properties["LoopCount"] = maxItProp.GetInt32();
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
                    if (jsonNode.TryGetProperty("expected_packet_id", out var expProp))
                    {
                        var packetIdStr = expProp.GetString();
                        if (!string.IsNullOrEmpty(packetIdStr) &&
                            Enum.TryParse<PacketId>(packetIdStr, true, out var packetId))
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

        private void AITreeGenerator_Click(object sender, RoutedEventArgs e)
            => ShowAiTreeGenerator();
    }
}