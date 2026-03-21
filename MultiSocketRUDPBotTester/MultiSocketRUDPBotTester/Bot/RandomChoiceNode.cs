using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ChoiceOption
    {
        public string Name { get; set; } = "";
        public int Weight { get; set; } = 1;
        public ActionNodeBase? Node { get; set; }
    }

    public class RandomChoiceNode : ActionNodeBase
    {
        public List<ChoiceOption> Choices { get; set; } = [];
        private static readonly Random Random = new();

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            if (Choices.Count == 0)
            {
                Log.Warning("RandomChoiceNode: No choices defined");
                return;
            }

            var totalWeight = Choices.Sum(c => c.Weight);
            if (totalWeight <= 0)
            {
                Log.Warning("RandomChoiceNode: Total weight is 0 or negative");
                return;
            }

            var randomValue = Random.Shared.Next(0, totalWeight);
            var cumulativeWeight = 0;
            ChoiceOption? selectedChoice = null;

            foreach (var choice in Choices)
            {
                cumulativeWeight += choice.Weight;
                if (randomValue >= cumulativeWeight)
                {
                    continue;
                }

                selectedChoice = choice;
                break;
            }

            if (selectedChoice?.Node != null)
            {
                Log.Information("RandomChoiceNode: Selected '{ChoiceName}' (weight: {Weight}/{TotalWeight})",
                    selectedChoice.Name, selectedChoice.Weight, totalWeight);
                selectedChoice.Node.Execute(client, receivedPacket);
            }
            else
            {
                Log.Warning("RandomChoiceNode: No choice was selected");
            }

            foreach (var nextNode in NextNodes)
            {
                nextNode.Execute(client, receivedPacket);
            }
        }
    }
}