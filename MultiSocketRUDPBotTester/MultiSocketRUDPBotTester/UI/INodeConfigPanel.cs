using MultiSocketRUDPBotTester.Bot;
using System.Windows.Controls;

namespace MultiSocketRUDPBotTester.UI
{
    /// <summary>
    /// Builds the editor UI for one graph-node type.
    /// </summary>
    public interface INodeConfigPanel
    {
        bool CanConfigure(NodeVisual node);
        void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog);
    }
}
