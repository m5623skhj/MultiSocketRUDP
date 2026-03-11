using System.Windows;
using MultiSocketRUDPBotTester.AI;
using MultiSocketRUDPBotTester.Bot;
using WpfCanvas = System.Windows.Controls.Canvas;

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

            AiTreeResponse? lastTree = null;

            var window = AiTreeWindowBuilder.Build(
                owner: this,
                out var inputBox,
                out var outputBox,
                out var applyBtn);

            var generateBtn = AiTreeWindowBuilder.GetGenerateButton(window);

            generateBtn.Click += async (_, _) =>
            {
                generateBtn.IsEnabled = false;
                applyBtn.IsEnabled = false;

                await HandleGenerate(
                    inputBox.Text.Trim(),
                    outputBox,
                    result =>
                    {
                        lastTree = result;
                        applyBtn.IsEnabled = result != null;
                    });

                generateBtn.IsEnabled = true;
            };

            applyBtn.Click += (_, _) => HandleApply(lastTree, window);

            window.Closed += (_, _) => aiGeneratorWindow = null;
            aiGeneratorWindow = window;
            window.Show();
        }

        private async Task HandleGenerate(
            string userInput,
            System.Windows.Controls.TextBox outputBox,
            Action<AiTreeResponse?> setLastTree)
        {
            if (string.IsNullOrEmpty(userInput))
            {
                MessageBox.Show("Please enter a test scenario description.", "Input Required",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

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
        }

        private void HandleApply(AiTreeResponse? lastTree, Window window)
        {
            if (lastTree == null) return;

            try
            {
                ClearAllNodesExceptRoot();

                var factory = new AiNodeFactory(
                    canvas: GraphCanvas,
                    allNodes: allNodes,
                    createNodeVisual: CreateNodeVisual,
                    createInputPort: CreateInputPort,
                    createOutputPort: CreateOutputPort,
                    addNodeToCanvas: AddNodeToCanvas,
                    log: Log);

                var rootVisual = allNodes.FirstOrDefault(n => n.IsRoot)
                    ?? throw new InvalidOperationException("Root node not found");

                var startX = WpfCanvas.GetLeft(rootVisual.Border) + 200;
                var startY = WpfCanvas.GetTop(rootVisual.Border);
                var createdNodes = new Dictionary<string, NodeVisual>();

                var firstNode = factory.CreateFromJson(
                    lastTree.RootNode, startX, startY, createdNodes, "root");

                if (firstNode != null)
                {
                    rootVisual.Next = firstNode;
                    rootVisual.NextPortType = "default";
                }

                Dispatcher.InvokeAsync(
                    () =>
                    {
                        renderer.RedrawConnections();
                        Log($"AI tree applied to canvas with {createdNodes.Count} nodes");
                    },
                    System.Windows.Threading.DispatcherPriority.Loaded);

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
            foreach (var node in allNodes.Where(n => !n.IsRoot).ToList())
            {
                GraphCanvas.Children.Remove(node.Border);
                GraphCanvas.Children.Remove(node.InputPort);
                if (node.OutputPort != null) GraphCanvas.Children.Remove(node.OutputPort);
                if (node.OutputPortTrue != null) GraphCanvas.Children.Remove(node.OutputPortTrue);
                if (node.OutputPortFalse != null) GraphCanvas.Children.Remove(node.OutputPortFalse);
                foreach (var port in node.DynamicOutputPorts)
                    GraphCanvas.Children.Remove(port);

                if (node.Border.ContextMenu != null)
                {
                    node.Border.ContextMenu.Items.Clear();
                    node.Border.ContextMenu = null;
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

            renderer.RedrawConnections();
            Log("Cleared all nodes except root");
        }

        private void AITreeGenerator_Click(object sender, RoutedEventArgs e)
            => ShowAiTreeGenerator();
    }
}
